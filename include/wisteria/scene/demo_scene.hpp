#pragma once

class AnimationClip;
class ModelAsset;
class ResourceManager;
class Scene;
class Window;

// The default demo is one complete MMD character pipeline: model import,
// full-body animation or assets/motions/demo.vmd, Morph, IK, Bullet physics,
// after-physics pose solving and rendering.
void SetupMmdCharacterDemo(
    Scene& scene,
    ResourceManager& resources,
    Window& window,
    bool alternateModel = false,
    bool useCompat = false
);

// Saba-backed demo window: saba::PMXModel drives animation/IK/morph and CPU
// skinning, then uploads skinned vertices into WISTERIA's Mesh every frame.
void SetupSabaMeshDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window
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
