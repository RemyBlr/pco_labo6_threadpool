#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <stack>
#include <vector>
#include <chrono>
#include <cassert>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <pcosynchro/pcohoaremonitor.h>

class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
    virtual void cancelRun() = 0;
    virtual std::string id() = 0;
};

class ThreadPool : public PcoHoareMonitor {
public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout) :
        maxThreadCount(maxThreadCount),
        maxNbWaiting(maxNbWaiting),
        idleTimeout(idleTimeout),
        currThreadsCount(0),
        stop(false) {
    }

    ~ThreadPool() {
        // TODO : End smoothly
        monitorIn();
        stop = true;
        // wake up trhread that might be waiting
        signal(availableTasks);
        signal(queueNotFull);
        monitorOut();

        // terminate threads
        for(auto &t : threads)
          t->join();
    }

    /*
     * Start a runnable. If a thread in the pool is avaible, assign the
     * runnable to it. If no thread is available but the pool can grow, create a new
     * pool thread and assign the runnable to it. If no thread is available and the
     * pool is at max capacity and there are less than maxNbWaiting threads waiting,
     * block the caller until a thread becomes available again, and else do not run the runnable.
     * If the runnable has been started, returns true, and else (the last case), return false.
     */
    bool start(std::unique_ptr<Runnable> runnable) {
        // TODO
        monitorIn();

        if(stop) {
            monitorOut(); // pool stopping, don't want another task
            runnable->cancelRun();
            return false;
        }

        // no thread and still place left
        if(currThreadsCount < maxThreadCount)
            createThread();

        // check if Q is full
        // 1) if already max, leave
        if (tasks.size() >= maxNbWaiting) {
            monitorOut();
            runnable->cancelRun();
            return false;
        }

        // 2) otherwise, wait for doWork to free space
        while(!stop && tasks.size() >= maxNbWaiting)
            wait(queueNotFull); // block queue until spot liberated


        tasks.push(std::move(runnable)); // free spot in queue
        signal(availableTasks); // wake up thread

        monitorOut();
        return true;
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        // TODO
        monitorIn();
        size_t count = currThreadsCount;
        monitorOut();
        return count;
    }

private:

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;

    std::queue<std::unique_ptr<Runnable>> tasks;
    std::vector<PcoThread*> threads;

    bool stop;
    size_t currThreadsCount;

    Condition availableTasks; // adding task, wake up thread
    Condition queueNotFull;   // spot in queue, start() can add new task

    void createThread() {
        threads.emplace_back(new PcoThread(&ThreadPool::doWork, this));
        currThreadsCount++;
    }

    void doWork() {
        // keep track of inacctivity
        auto start = std::chrono::steady_clock::now();

        monitorIn();
        bool timeout = false;
        while(true) {
            while(!stop && tasks.empty() && !timeout) {
                monitorOut(); // get out, so we dont block everyone
                PcoThread::usleep(1000); // polling cycle
                monitorIn();

                if(!tasks.empty())
                    break; // process task

                // check inactivity timeout
                auto now = std::chrono::steady_clock::now();
                auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now-start);
                if(idle >= idleTimeout && tasks.empty()) //TODO TEST
                    timeout = true;
            }

            if(timeout || (stop && tasks.empty()))
                break; // leave timeout or no more tasks

            if(!tasks.empty()) {
                auto task = std::move(tasks.front());
                tasks.pop();
                signal(queueNotFull); // empty spot in queue
					 std::cout<<"Tache: "<<task->id() << std::endl; //TODO TEST
                monitorOut();

                // execute task
                task->run();
                start = std::chrono::steady_clock::now();

                monitorIn(); // new loop
            }
            else
                break; // stop and empty queue //TODO TEST
        }
        currThreadsCount--;
        monitorOut(); // thread ends here
    }
};

#endif // THREADPOOL_H
