#pragma once

#include <filesystem>

namespace wisteria
{
class ResourceManager;
class Scene;
class Window;

// Default demo: Saba drives the whole MMD chain (VMD animation, IK, morph,
// physics and CPU skinning), uploads skinned vertices into WISTERIA's Mesh,
// and plays a VMD camera track (梦的翅膀 motion + camera loop).
void SetupSabaMmdDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window,
    bool alternateModel = false,
    std::filesystem::path modelPath = {},
    std::filesystem::path scenePath = {},
    bool sceneMode = false,
    std::filesystem::path motionPath = {},
    float physicsFps = 0.0f,
    int maxSubSteps = 0
);

// Minimal ground-shadow test scene: a fixed camera, a clearly visible ground
// plane and a shadow-casting cube. No VMD camera, no character, no skybox.
void SetupGroundShadowLabScene(
    Scene& scene,
    ResourceManager& resources
);
}  // namespace wisteria
