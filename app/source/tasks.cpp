#include "tasks.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

/// Три потока: обложки читаются по одной на плитку, и больше трёх
/// одновременных чтений с romfs всё равно упираются в носитель.
constexpr int IO_WORKERS = 3;

struct Queue
{
    std::deque<std::function<void()>> items;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_bool running { false };

    void push(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            items.push_back(std::move(task));
        }
        cv.notify_one();
    }

    /// Забирает задачу, ожидая появления. Пустая функция означает «пора выйти».
    std::function<void()> pop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !items.empty() || !running.load(); });
        if (!running.load() && items.empty())
            return {};
        std::function<void()> task = std::move(items.front());
        items.pop_front();
        return task;
    }

    void wake()
    {
        cv.notify_all();
    }
};

Queue ioQueue;
Queue heavyQueue;
std::vector<std::thread> workers;

void run(Queue& queue)
{
    while (true)
    {
        std::function<void()> task = queue.pop();
        if (!task)
            return;
        task();
    }
}

}  // namespace

namespace tasks
{

void start()
{
    if (!workers.empty())
        return;

    ioQueue.running    = true;
    heavyQueue.running = true;

    for (int i = 0; i < IO_WORKERS; i++)
        workers.emplace_back([] { run(ioQueue); });
    workers.emplace_back([] { run(heavyQueue); });
}

void stop()
{
    ioQueue.running    = false;
    heavyQueue.running = false;
    ioQueue.wake();
    heavyQueue.wake();

    for (std::thread& worker : workers)
        if (worker.joinable())
            worker.join();
    workers.clear();
}

void io(std::function<void()> task)
{
    ioQueue.push(std::move(task));
}

void heavy(std::function<void()> task)
{
    heavyQueue.push(std::move(task));
}

}  // namespace tasks
