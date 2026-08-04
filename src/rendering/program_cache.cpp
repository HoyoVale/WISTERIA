#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/program_cache.hpp"
#include "wisteria/rendering/shader.hpp"

namespace wisteria
{
std::shared_ptr<Program> ProgramCache::Acquire(
    const std::string& vertexPath,
    const std::string& fragmentPath
)
{
    const std::string key = vertexPath + "\n" + fragmentPath;
    const auto cached = this->programs.find(key);
    if (cached != this->programs.end())
        return cached->second;

    auto shader = std::make_unique<Shader>(vertexPath, fragmentPath);
    auto program = std::make_shared<Program>(shader->GetShaderList());
    this->programs.emplace(key, program);
    return program;
}

void ProgramCache::Clear() noexcept
{
    this->programs.clear();
}

std::size_t ProgramCache::Size() const noexcept
{
    return this->programs.size();
}
}  // namespace wisteria
