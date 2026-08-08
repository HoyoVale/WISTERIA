#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <stdexcept>
#include <vector>

namespace wisteria
{
ModelFrameView IModelRuntimeDriver::ProduceFrameView() const
{
    return ModelFrameView{
        this->VertexFrame(),
        this->TryGetPose(),
        0U
    };
}

Pose& IModelRuntimeDriver::GetPose()
{
    Pose* result = this->TryGetPose();
    if (result == nullptr)
        throw std::logic_error("Runtime has no skeleton pose");
    return *result;
}

const Pose& IModelRuntimeDriver::GetPose() const
{
    const Pose* result = this->TryGetPose();
    if (result == nullptr)
        throw std::logic_error("Runtime has no skeleton pose");
    return *result;
}

ModelRuntimeCapabilities IModelRuntimeDriver::Capabilities() const
{
    // Default: no capabilities advertised. Backends override with what they
    // actually support.
    return {};
}

ModelPhysicsRuntimeInfo IModelRuntimeDriver::PhysicsInfo() const
{
    ModelPhysicsRuntimeInfo info;
    info.available = this->TryGetPhysicsInstance() != nullptr;
    info.ownsSimulationStep = info.available
        ? this->TryGetPhysicsInstance()->OwnsSimulationStep()
        : false;
    return info;
}
}  // namespace wisteria
