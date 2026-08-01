#pragma once

class ResourceManager;
class Scene;

// Builds the current model-viewer scene. Keeping this outside Window makes the
// platform window reusable by tests, tools, and future secondary viewports.
void SetupDemoScene(Scene& scene, ResourceManager& resources);
