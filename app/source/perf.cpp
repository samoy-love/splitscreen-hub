#include "perf.hpp"

#include <borealis.hpp>

#include <array>
#include <atomic>
#include <chrono>

namespace
{

long long nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Атомарные: обложки считаются из трёх io-потоков одновременно. Размер с
/// запасом — счётчики добавляются чаще, чем про него вспоминают.
std::array<std::atomic<long long>, 16> counters {};

}  // namespace

namespace perf
{

Scope::Scope(std::string what, double thresholdMs)
    : what(std::move(what))
    , threshold(thresholdMs)
    , startedUs(nowUs())
{
}

double Scope::elapsedMs() const
{
    return (nowUs() - startedUs) / 1000.0;
}

Scope::~Scope()
{
    const double ms = elapsedMs();
    if (!what.empty() && ms >= threshold)
        brls::Logger::info("[время] {}: {:.1f} мс", what, ms);
}

void count(Counter counter, long long value)
{
    counters[static_cast<size_t>(counter)].fetch_add(value, std::memory_order_relaxed);
}

void report()
{
    auto get = [](Counter c) { return counters[static_cast<size_t>(c)].load(); };

    const long long fromCache = get(Counter::CoverFromCache);
    const long long fromDisk  = get(Counter::CoverFromDisk);
    const long long total     = fromCache + fromDisk;

    brls::Logger::info("[итог] обложки: из кэша {}, с карты {}, отменено {}"
                       "{}",
                       fromCache, fromDisk, get(Counter::CoverDropped),
                       total > 0 ? fmt::format(" — попаданий {}%", fromCache * 100 / total) : "");

    if (fromDisk > 0)
        brls::Logger::info("[итог] разбор JPEG: {} мс всего, {:.1f} мс в среднем",
                           get(Counter::CoverDecodeMs),
                           double(get(Counter::CoverDecodeMs)) / double(fromDisk));

    const long long queries = get(Counter::CatalogQueries);
    if (queries > 0)
        brls::Logger::info("[итог] каталог: {} выборок, {} мс всего, {:.1f} мс в среднем", queries,
                           get(Counter::CatalogMs), double(get(Counter::CatalogMs)) / double(queries));

    const long long fetches = get(Counter::NetFetches);
    if (fetches > 0)
        brls::Logger::info("[итог] сеть: {} загрузок, {} мс всего, {:.0f} мс в среднем", fetches,
                           get(Counter::NetMs), double(get(Counter::NetMs)) / double(fetches));

    const long long frames = get(Counter::Frames);
    if (frames > 0)
        brls::Logger::info("[итог] кадры: {} всего, из них с рывком {} ({:.1f}%)", frames,
                           get(Counter::FramesSlow),
                           double(get(Counter::FramesSlow)) * 100.0 / double(frames));
}

}  // namespace perf
