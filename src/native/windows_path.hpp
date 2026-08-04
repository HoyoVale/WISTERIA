#pragma once

#include <string>

#ifdef _WIN32
// UTF-8 -> UTF-16 conversion isolated in its own translation unit so the
// Windows SDK headers never meet the project's global rendering enums
// (vao.hpp defines FLOAT/INT/UINT/UCHAR, which collide with minwindef.h).
std::wstring WisteriaNativeUtf8ToWide(const char* utf8);
#endif
