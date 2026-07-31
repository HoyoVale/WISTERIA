#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraParam{
    glm::vec3 Position{0.0f, 0.0f, 3.0f};
    glm::vec3 Target{0.0f, 0.0f, 0.0f};
    glm::vec3 Up{0.0f, 1.0f, 0.0f};
};

class Camera{
public:
    explicit Camera(const CameraParam& cameraParam = {});

    glm::mat4 GetView() const;
    CameraParam& GetParam() { return this->param; }; 
    const CameraParam& GetParam() const{ return this->param; };
private:
    CameraParam param;
};

