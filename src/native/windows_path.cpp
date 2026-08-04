#include "windows_path.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

std::wstring WisteriaNativeUtf8ToWide(const char* utf8)
{
    if (utf8 == nullptr)
        return {};
    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8,
        -1,
        nullptr,
        0
    );
    if (wideLength <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8,
            -1,
            wide.data(),
            wideLength
        ) <= 0)
    {
        return {};
    }
    return wide;
}
#endif
