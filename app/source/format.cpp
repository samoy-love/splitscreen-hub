#include "format.hpp"

#include <cstdio>

namespace fmtx
{

bool sizeInGigabytes(long long bytes, double& value)
{
    value = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (value >= 1.0)
        return true;
    value = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return false;
}

std::string formatSize(long long bytes, const char* gigabytes, const char* megabytes)
{
    if (bytes <= 0)
        return "";

    double value = 0;
    const bool gb = sizeInGigabytes(bytes, value);

    char buf[32];
    std::snprintf(buf, sizeof(buf), gb ? "%.1f %s" : "%.0f %s", value,
                  gb ? gigabytes : megabytes);
    return buf;
}

int languageCount(const std::string& languages)
{
    if (languages.empty())
        return 0;
    int n = 1;
    for (char c : languages)
        n += c == ',';
    return n;
}

bool parseHexColor(const std::string& hex, unsigned char& r, unsigned char& g, unsigned char& b)
{
    if (hex.size() != 6)
        return false;

    unsigned value = 0;
    for (char c : hex)
    {
        unsigned digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<unsigned>(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<unsigned>(c - 'A') + 10;
        else
            return false;  // strtol пропускал бы «0x», пробелы и знак

        value = value * 16 + digit;
    }

    r = static_cast<unsigned char>((value >> 16) & 0xFF);
    g = static_cast<unsigned char>((value >> 8) & 0xFF);
    b = static_cast<unsigned char>(value & 0xFF);
    return true;
}

}  // namespace fmtx
