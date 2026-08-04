#pragma once

#include <filesystem>

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
