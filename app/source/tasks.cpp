#include "tasks.hpp"

#include <borealis.hpp>

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
    const char* name = "";
    std::deque<std::function<void()>> items;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_bool running { false };

    void push(std::function<void()> task)
    {
        size_t depth;
        {
            std::lock_guard<std::mutex> lock(mutex);
            items.push_back(std::move(task));
            depth = items.size();
        }
        brls::Logger::verbose("tasks[{}]: +задача, в очереди {}", name, depth);
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

/// Отдельно от Queue::running: очереди можно остановить и заново запустить, а
/// этот флаг означает именно завершение работы приложения.
std::atomic_bool stopping { false };

Queue ioQueue { "io" };
Queue heavyQueue { "heavy" };
std::vector<std::thread> workers;

void run(Queue& queue)
{
    brls::Logger::debug("tasks[{}]: рабочий поток запущен", queue.name);

    while (true)
    {
        std::function<void()> task = queue.pop();
        if (!task)
        {
            brls::Logger::debug("tasks[{}]: рабочий поток завершён", queue.name);
            return;
        }

        // Без перехвата брошенное задание уносит весь рабочий поток, и очередь
        // после этого просто перестаёт разбираться — молча, без единого следа.
        try
        {
            task();
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("tasks[{}]: задание бросило исключение: {}", queue.name, e.what());
        }
        catch (...)
        {
            brls::Logger::error("tasks[{}]: задание бросило неизвестное исключение", queue.name);
        }
    }
}

}  // namespace

namespace tasks
{

void start()
{
    if (!workers.empty())
        return;

    stopping           = false;
    ioQueue.running    = true;
    heavyQueue.running = true;

    for (int i = 0; i < IO_WORKERS; i++)
        workers.emplace_back([] { run(ioQueue); });
    workers.emplace_back([] { run(heavyQueue); });

    brls::Logger::info("tasks: запущено потоков: {} (io {} + heavy 1)", workers.size(), IO_WORKERS);
}

bool shuttingDown()
{
    return stopping.load();
}

void requestStop()
{
    stopping = true;
}

void stop()
{
    stopping = true;

    if (workers.empty())
        return;

    brls::Logger::info("tasks: остановка {} потоков", workers.size());
    ioQueue.running    = false;
    heavyQueue.running = false;
    ioQueue.wake();
    heavyQueue.wake();

    for (std::thread& worker : workers)
        if (worker.joinable())
            worker.join();
    workers.clear();
    brls::Logger::info("tasks: все потоки остановлены");
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
