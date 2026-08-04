#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace wisteria
{
inline bool PathEqualsIgnoreCase(
    const std::string& left,
    const std::string& right
)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        const unsigned char leftByte =
            static_cast<unsigned char>(left[index]);
        const unsigned char rightByte =
            static_cast<unsigned char>(right[index]);
        if (std::tolower(leftByte) != std::tolower(rightByte))
            return false;
    }
    return true;
}

// Walks the path from its root, resolving each component case-insensitively
// against the directory listing. Returns the original path when no match
// exists, so callers keep their original error reporting.
inline std::filesystem::path ResolvePathCaseInsensitive(
    const std::filesystem::path& requested
)
{
    if (std::filesystem::exists(requested))
        return requested;

    std::vector<std::string> components;
    for (const std::filesystem::path& part : requested.relative_path())
        components.push_back(part.string());

    std::filesystem::path current = requested.root_path();
    std::size_t resolved = 0U;
    while (resolved < components.size())
    {
        const std::filesystem::path exact = current / components[resolved];
        if (std::filesystem::exists(exact))
        {
            current = exact;
            ++resolved;
            continue;
        }
        if (!std::filesystem::is_directory(current))
            break;
        bool matched = false;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(current))
        {
            if (PathEqualsIgnoreCase(
                    entry.path().filename().string(),
                    components[resolved]
                ))
            {
                current = entry.path();
                ++resolved;
                matched = true;
                break;
            }
        }
        if (!matched)
            break;
    }
    return resolved == components.size() ? current : requested;
}
}
