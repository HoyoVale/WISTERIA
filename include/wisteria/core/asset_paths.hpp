#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace wisteria::assets
{
inline std::filesystem::path Root()
{
    if (const char* configured = std::getenv("WISTERIA_ASSET_ROOT"))
    {
        if (configured[0] != '\0')
            return std::filesystem::path(configured);
    }
    return std::filesystem::current_path() / "assets";
}

inline std::filesystem::path Resolve(
    std::string_view category,
    std::string_view fileName
)
{
    return Root() / category / fileName;
}

inline std::string Shader(std::string_view fileName)
{
    return Resolve("shaders", fileName).string();
}

inline std::string Texture(std::string_view fileName)
{
    return Resolve("textures", fileName).string();
}
}
