#pragma once

class ModelAsset;
class ResourceManager;
class Scene;
class Window;

// Builds the current model-viewer scenes. Keeping this outside Window makes
// the platform window reusable by tests, tools, and future secondary views.
void SetupDemoScene1(Scene& scene, ResourceManager& resources);
void SetupDemoScene2(Scene& scene, ResourceManager& resources);

// Creates a small procedural, skinned model containing every supported MMD
// Morph kind. It is public so automated tests can validate the same asset used
// by the interactive demo instead of maintaining a separate test-only copy.
ModelAsset& CreateMorphLabModel(ResourceManager& resources);
void SetupMorphDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window
);
