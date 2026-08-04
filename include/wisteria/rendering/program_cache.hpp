#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace wisteria
{
class Program;

// One cache belongs to one OpenGL resource share group. It must be destroyed
// while a context in that share group is current.
class ProgramCache
{
public:
    std::shared_ptr<Program> Acquire(
        const std::string& vertexPath,
        const std::string& fragmentPath
    );
    void Clear() noexcept;
    std::size_t Size() const noexcept;

private:
    std::unordered_map<std::string, std::shared_ptr<Program>> programs;
};
}  // namespace wisteria
