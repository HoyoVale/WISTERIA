#pragma once

#include "wisteria/assets/model_asset.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/runtime/runtime_creation_options.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>

namespace wisteria
{
class IModelBackend
{
public:
    virtual ~IModelBackend() = default;
    virtual ModelBackendKind Kind() const noexcept = 0;
    virtual std::string_view Name() const noexcept = 0;
    virtual std::unique_ptr<IModelRuntimeDriver> CreateRuntime(
        const ModelAsset& asset,
        const RuntimeCreationOptions& options = {}
    ) const = 0;
};

class ModelBackendRegistry
{
public:
    ModelBackendRegistry() = default;
    ~ModelBackendRegistry() = default;

    ModelBackendRegistry(const ModelBackendRegistry&) = delete;
    ModelBackendRegistry& operator=(const ModelBackendRegistry&) = delete;
    ModelBackendRegistry(ModelBackendRegistry&&) noexcept = default;
    ModelBackendRegistry& operator=(ModelBackendRegistry&&) noexcept = default;

    void Register(std::unique_ptr<IModelBackend> backend);
    const IModelBackend* Find(ModelBackendKind kind) const noexcept;
    std::unique_ptr<IModelRuntimeDriver> CreateRuntime(
        const ModelAsset& asset,
        const RuntimeCreationOptions& options = {}
    ) const;

private:
    struct EnumHash
    {
        std::size_t operator()(ModelBackendKind value) const noexcept
        {
            return static_cast<std::size_t>(value);
        }
    };
    std::unordered_map<
        ModelBackendKind,
        std::unique_ptr<IModelBackend>,
        EnumHash
    > backends;
};

void RegisterDefaultModelBackends(ModelBackendRegistry& registry);
}  // namespace wisteria
