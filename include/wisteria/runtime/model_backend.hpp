#pragma once

#include "wisteria/assets/model_asset.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>

namespace wisteria
{
// R1.4 Phase 0A: WISTERIA-governed runtime creation options. Selects
// semantic profiles and stable physics settings; never concrete backend
// implementations (contract §2B: "Saba executes; WISTERIA governs").
struct RuntimeCreationOptions
{
    MmdPhysicsPreset physicsPreset = MmdPhysicsPreset::MmdRaw;
    MmdPhysicsRuntimeSettings physicsSettings;
    std::uint32_t reserved[4] = {0U, 0U, 0U, 0U};
};

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
