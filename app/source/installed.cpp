#include "installed.hpp"

#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>

#include <cstdio>
#endif

namespace installed
{

std::set<std::string> titleIds()
{
    std::set<std::string> out;

#ifdef __SWITCH__
    if (R_FAILED(nsInitialize()))
    {
        brls::Logger::error("ns:am не инициализировался, список установленных игр недоступен");
        return out;
    }

    // записей может быть много, читаем порциями
    constexpr int BATCH = 64;
    NsApplicationRecord records[BATCH];
    int offset = 0;

    while (true)
    {
        int count  = 0;
        Result res = nsListApplicationRecord(records, BATCH, offset, &count);
        if (R_FAILED(res))
        {
            brls::Logger::warning("ns:am: чтение с позиции {} не удалось (0x{:x})", offset,
                                  (unsigned)res);
            break;
        }
        if (count <= 0)
            break;

        for (int i = 0; i < count; i++)
        {
            char id[17];
            std::snprintf(id, sizeof(id), "%016lx", records[i].application_id);
            out.insert(id);
        }

        offset += count;
        if (count < BATCH)
            break;
    }

    nsExit();
    brls::Logger::info("Установленных игр на консоли: {}", out.size());
#endif

    return out;
}

}  // namespace installed
