#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraParam{
    glm::vec3 Position{0.0f, 0.0f, 3.0f};
    glm::vec3 Target{0.0f, 0.0f, 0.0f};
    glm::vec3 Up{0.0f, 1.0f, 0.0f};
    float VerticalFovDegrees = 45.0f;
    float NearClip = 0.1f;
    float FarClip = 1000.0f;
};

class Camera{
public:
    explicit Camera(const CameraParam& cameraParam = {});

    glm::mat4 GetView() const;
    glm::mat4 GetProjection(float aspect) const;
    const CameraParam& GetParam() const noexcept { return this->param; }
    const glm::vec3& Position() const noexcept { return this->param.Position; }
    const glm::vec3& Target() const noexcept { return this->param.Target; }
    const glm::vec3& Up() const noexcept { return this->param.Up; }
    float VerticalFovDegrees() const noexcept {
        return this->param.VerticalFovDegrees;
    }
    float NearClip() const noexcept { return this->param.NearClip; }
    float FarClip() const noexcept { return this->param.FarClip; }

    void SetParam(const CameraParam& cameraParam);
    void SetPosition(const glm::vec3& position);
    void SetTarget(const glm::vec3& target);
    void SetUp(const glm::vec3& up);
    void SetVerticalFovDegrees(float verticalFovDegrees);
    void SetClipPlanes(float nearClip, float farClip);
private:
    CameraParam param;
};
