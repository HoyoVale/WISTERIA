#pragma once

#include <filesystem>

class AnimationClip;
class ModelAsset;
class ResourceManager;
class Scene;
class Window;

// Default demo: Saba drives the whole MMD chain (VMD animation, IK, morph,
// physics and CPU skinning), then uploads skinned vertices into WISTERIA's
// Mesh every frame.
void SetupSabaMmdDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window,
    bool alternateModel = false,
    std::filesystem::path modelPath = {}
);

// Creates the procedural fallback used when assets/motions/demo.vmd is absent.
// Kept public so tests validate the exact clip used by the interactive demo.
const AnimationClip& CreateMmdFullBodyDemoAnimation(ModelAsset& model);

// Morph Lab remains an opt-in diagnostics scene rather than a default window.
ModelAsset& CreateMorphLabModel(ResourceManager& resources);
void SetupMorphDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window
);
