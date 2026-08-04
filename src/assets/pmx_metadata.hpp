#pragma once

// Internal model-import metadata: PMX binary structures parsed from a PMX
// file. This header lives in src/ because it is an implementation detail of
// the asset importer, not part of the public WISTERIA API.

#include "wisteria/animation/morph.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PmxMaterialMetadata
{
    std::string name;
    glm::vec4 diffuse{1.0f};
    glm::vec3 specular{0.0f};
    float specularPower = 1.0f;
    glm::vec3 ambient{0.0f};
    bool doubleSided = false;
    bool edgeEnabled = false;
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;
    std::optional<std::string> diffuseTexture;
    std::optional<std::string> sphereTexture;
    MmdSphereMapMode sphereMode = MmdSphereMapMode::Disabled;
    std::optional<std::string> toonTexture;
    std::optional<unsigned int> commonToonIndex;
};

struct PmxMetadata
{
    std::vector<PmxMaterialMetadata> materials;
    std::vector<std::vector<float>> materialVertexEdgeScales;
    std::vector<std::vector<std::uint32_t>> materialSourceVertexIndices;
    struct IkLink
    {
        int boneIndex = -1;
        bool hasLimits = false;
        glm::vec3 minimumAngle{0.0f};
        glm::vec3 maximumAngle{0.0f};
    };

    struct BoneMetadata
    {
        std::string name;
        std::int32_t deformLayer = 0;
        std::uint16_t flags = 0U;
        int appendSourceIndex = -1;
        float appendWeight = 0.0f;
        int ikTargetIndex = -1;
        std::uint32_t ikIterations = 0U;
        float ikAngleLimit = 0.0f;
        std::vector<IkLink> ikLinks;
    };

    struct VertexMorphMetadata
    {
        MorphIndex morphIndex = InvalidMorphIndex;
        std::string name;
        MorphCategory category = MorphCategory::Other;
        std::vector<std::pair<std::uint32_t, glm::vec3>> offsets;
    };

    struct UvMorphMetadata
    {
        MorphIndex morphIndex = InvalidMorphIndex;
        std::uint8_t channel = 0U;
        std::vector<std::pair<std::uint32_t, glm::vec4>> offsets;
    };

    struct RigidBodyMetadata
    {
        std::string name;
        int boneIndex = -1;
        std::uint8_t collisionGroup = 0U;
        std::uint16_t nonCollisionMask = 0U;
        MmdRigidBodyShape shape = MmdRigidBodyShape::Sphere;
        glm::vec3 size{0.0f};
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float mass = 0.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float restitution = 0.0f;
        float friction = 0.0f;
        MmdRigidBodyMode mode = MmdRigidBodyMode::FollowBone;
    };

    struct JointMetadata
    {
        std::string name;
        MmdJointType type = MmdJointType::Spring6Dof;
        int bodyA = -1;
        int bodyB = -1;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linearLower{0.0f};
        glm::vec3 linearUpper{0.0f};
        glm::vec3 angularLower{0.0f};
        glm::vec3 angularUpper{0.0f};
        glm::vec3 linearSpring{0.0f};
        glm::vec3 angularSpring{0.0f};
    };

    std::vector<BoneMetadata> bones;
    std::vector<std::uint8_t> assimpCompatibleBytes;
    std::vector<RigidBodyMetadata> rigidBodies;
    std::vector<JointMetadata> joints;
    std::vector<MorphDefinition> morphDefinitions;
    std::vector<VertexMorphMetadata> vertexMorphs;
    std::vector<UvMorphMetadata> uvMorphs;
};

