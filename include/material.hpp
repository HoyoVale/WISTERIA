#pragma once
#include "shader.hpp"
#include "texture.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <unordered_map>

inline const std::string textureRootPath =
    (std::filesystem::current_path() / "assets" / "textures").string() + "\\";


struct MaterialData{
    Path shaderFilePath;
    std::unordered_map<std::string, std::string> textureFilePath = {
        {"texture", textureRootPath + "chessboard.png"}
    };
    glm::vec3 specularColor = {1.0f, 1.0f, 1.0f}; // Specular reflection color.
    float shininess = 32.0f; // Higher values make a sharper highlight.
};

class Material{
public:
    explicit Material(const MaterialData &_data = {});
    ~Material() = default;

    void Attach();
    void Bind();
    void Unbind();

    Program& GetProgram();
    const Program& GetProgram() const;

    const glm::vec3& SpecularColor() const noexcept;
    float Shininess() const noexcept;

    void SetSpecularColor(const glm::vec3& color) noexcept;
    void SetShininess(float shininess) noexcept;

private:
    std::unique_ptr<Shader> shader;
    std::unique_ptr<Program> program;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    MaterialData data;
};
