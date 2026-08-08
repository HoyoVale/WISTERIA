#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/runtime/wisteria_generic_runtime_driver.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"

#include <stdexcept>
#include <utility>

namespace wisteria
{
namespace
{
class SabaMmdBackend final : public IModelBackend
{
public:
    ModelBackendKind Kind() const noexcept override
    {
        return ModelBackendKind::SabaMmd;
    }

    std::string_view Name() const noexcept override
    {
        return "saba-mmd";
    }

    std::unique_ptr<IModelRuntimeDriver> CreateRuntime(
        const ModelAsset& asset,
        const RuntimeCreationOptions& options
    ) const override
    {
        const ModelSourceDescriptor* source =
            asset.TryGetSourceDescriptor();
        if (source == nullptr || source->sourcePath.empty())
        {
            throw std::logic_error(
                "Saba MMD asset has no source descriptor"
            );
        }
        auto runtime = std::make_unique<SabaMmdRuntimeModel>(
            source->sourcePath
        );
        // R1.4: authoritative configuration is built from stable options and
        // applied before Initialize. A physics-settings override that
        // diverges from the frozen preset carries a custom identity.
        MmdPhysicsPreset preset = MmdPhysicsPreset::MmdRaw;
        switch (options.compatibility)
        {
            case RuntimeCompatibilityProfile::Raw:
                preset = MmdPhysicsPreset::MmdRaw;
                break;
            case RuntimeCompatibilityProfile::Community:
                preset = MmdPhysicsPreset::MmdCommunity;
                break;
            case RuntimeCompatibilityProfile::Adaptive:
                preset = MmdPhysicsPreset::WisteriaAdaptive;
                break;
        }
        MmdPhysicsConfiguration configuration =
            BuildPresetConfiguration(preset);
        configuration.runtime.fixedTimeStep = options.physics.fixedTimeStep;
        configuration.runtime.maxSubSteps = options.physics.maxSubSteps;
        configuration.runtime.gravity = options.physics.gravity;
        configuration.runtime.enabled = options.physics.enabled;
        if (ComputeEffectiveConfigurationFingerprint(configuration) !=
            ComputeEffectiveConfigurationFingerprint(
                BuildPresetConfiguration(preset)
            ))
        {
            configuration.identity.originPreset =
                ToPresetNameLower(preset);
        }
        const TimelineStatus configStatus =
            runtime->SetMmdPhysicsConfiguration(configuration);
        if (configStatus != TimelineStatus::Ok)
        {
            throw std::runtime_error(
                "Saba MMD runtime rejected creation options"
            );
        }
        runtime->SetAsset(&asset);
        if (!runtime->Initialize())
        {
            throw std::runtime_error(
                "Saba MMD runtime failed to initialize: " +
                source->sourcePath.string()
            );
        }
        return runtime;
    }
};

class WisteriaGenericBackend final : public IModelBackend
{
public:
    ModelBackendKind Kind() const noexcept override
    {
        return ModelBackendKind::WisteriaGeneric;
    }

    std::string_view Name() const noexcept override
    {
        return "wisteria-generic";
    }

    std::unique_ptr<IModelRuntimeDriver> CreateRuntime(
        const ModelAsset& asset,
        const RuntimeCreationOptions& options
    ) const override
    {
        (void)options;
        auto runtime =
            std::make_unique<WisteriaGenericRuntimeDriver>(asset);
        if (!runtime->Initialize())
        {
            throw std::runtime_error(
                "Wisteria generic runtime failed to initialize"
            );
        }
        return runtime;
    }
};
}

void ModelBackendRegistry::Register(std::unique_ptr<IModelBackend> backend)
{
    if (backend == nullptr)
        throw std::invalid_argument("Model backend must not be null");
    const ModelBackendKind kind = backend->Kind();
    if (kind == ModelBackendKind::Static)
    {
        throw std::invalid_argument(
            "Static models do not require a runtime backend"
        );
    }
    if (!this->backends.emplace(kind, std::move(backend)).second)
        throw std::logic_error("Model backend kind is already registered");
}

const IModelBackend* ModelBackendRegistry::Find(
    ModelBackendKind kind
) const noexcept
{
    const auto iterator = this->backends.find(kind);
    return iterator == this->backends.end() ? nullptr : iterator->second.get();
}

std::unique_ptr<IModelRuntimeDriver> ModelBackendRegistry::CreateRuntime(
    const ModelAsset& asset,
    const RuntimeCreationOptions& options
) const
{
    const ModelBackendKind kind = asset.BackendKind();
    if (kind == ModelBackendKind::Static)
        return {};
    const IModelBackend* backend = this->Find(kind);
    if (backend == nullptr)
        throw std::logic_error("No backend registered for model asset");
    return backend->CreateRuntime(asset, options);
}

void RegisterDefaultModelBackends(ModelBackendRegistry& registry)
{
    registry.Register(std::make_unique<SabaMmdBackend>());
    registry.Register(std::make_unique<WisteriaGenericBackend>());
}
}  // namespace wisteria
