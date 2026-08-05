#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

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
        const ModelAsset& asset
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
    const ModelAsset& asset
) const
{
    const ModelBackendKind kind = asset.BackendKind();
    if (kind == ModelBackendKind::Static)
        return {};
    const IModelBackend* backend = this->Find(kind);
    if (backend == nullptr)
        throw std::logic_error("No backend registered for model asset");
    return backend->CreateRuntime(asset);
}

void RegisterDefaultModelBackends(ModelBackendRegistry& registry)
{
    registry.Register(std::make_unique<SabaMmdBackend>());
}
}  // namespace wisteria
