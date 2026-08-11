#pragma once

// R2.0 Phase 0D Stage 1: frame-lifetime stable view of "what this frame
// draws". Extraction is pure CPU: it does not execute GL, does not update
// runtimes, does not build a RenderGraph, and must not change pass order or
// pixel results.

#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/physics/physics_types.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <vector>

namespace wisteria
{
class EnvironmentMap;
class MorphState;
class Pose;
class RenderPart;
class Scene;
class DirectionalLight;
class PointLight;
class SpotLight;

// One draw item: stable references into the scene/runtime state, resolved
// material morph values, and the final world transform.
struct RenderCommand
{
    RenderPart* part = nullptr;
    glm::mat4 model{1.0f};
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
    MaterialMorphValues material;
};

// R2.0 Phase 0D Stage 1: extracted frame data consumed by the existing
// Renderer. All pointers/spans remain valid only while the source Scene and
// ModelInstances are untouched; mutation during rendering is forbidden.
struct RenderFramePacket
{
    Camera camera;
    glm::mat4 projection{1.0f};

    std::vector<RenderCommand> opaqueDraws;
    std::vector<RenderCommand> transparentDraws;

    // Frame semantic view: light pointers, not Scene container
    // representation. No deep copies; valid for the packet lifetime.
    std::vector<const DirectionalLight*> directionalLights;
    std::vector<const PointLight*> pointLights;
    std::vector<const SpotLight*> spotLights;
    EnvironmentMap* environment = nullptr;

    // Physics debug lines collected at extraction time; the GPU pass never
    // traverses Scene/Physics/Entity again.
    std::vector<PhysicsDebugLine> debugLines;
};

// Builds the packet from scene/runtime state. No GL, no runtime mutation.
RenderFramePacket BuildRenderFramePacket(
    Scene& scene,
    const Camera& camera,
    const glm::mat4& projection
);
}  // namespace wisteria
