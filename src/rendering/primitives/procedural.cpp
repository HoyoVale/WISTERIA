#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/primitives/procedural.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace wisteria
{
namespace
{
constexpr std::size_t PrimitiveStride = 15U;  // pos3 color3 uv2 normal3 tangent4

void AppendVertex(
    DefaultModelData& data,
    const glm::vec3& position,
    const glm::vec3& normal,
    const glm::vec2& uv,
    const glm::vec3& tangent
)
{
    const float vertex[PrimitiveStride] = {
        position.x, position.y, position.z,
        1.0f, 1.0f, 1.0f,
        uv.x, uv.y,
        normal.x, normal.y, normal.z,
        tangent.x, tangent.y, tangent.z, 1.0f
    };
    for (std::size_t component = 0U; component < PrimitiveStride; ++component)
        data.vertices.push_back(vertex[component]);
}

void AppendTriangle(DefaultModelData& data, std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    data.indices.push_back(a);
    data.indices.push_back(b);
    data.indices.push_back(c);
}

glm::vec3 OrthogonalTangent(const glm::vec3& normal)
{
    const glm::vec3 reference =
        std::abs(normal.y) < 0.999f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(reference, normal));
}

void ConfigureLayout(DefaultModelData& data)
{
    data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT, false, false, 4U}
    };
}
}

DefaultModelData BuildSphereMeshData(float radius, int stacks, int slices)
{
    if (!std::isfinite(radius) || radius <= 0.0f ||
        stacks < 2 || slices < 3)
    {
        throw std::invalid_argument(
            "Sphere requires radius > 0, stacks >= 2, slices >= 3"
        );
    }
    DefaultModelData data;
    ConfigureLayout(data);

    for (int stack = 0; stack <= stacks; ++stack)
    {
        const float phi = static_cast<float>(stack) /
            static_cast<float>(stacks) * 3.14159265358979f;
        const float y = radius * std::cos(phi);
        const float ringRadius = radius * std::sin(phi);
        for (int slice = 0; slice <= slices; ++slice)
        {
            const float theta = static_cast<float>(slice) /
                static_cast<float>(slices) * 2.0f * 3.14159265358979f;
            const glm::vec3 normal(
                std::sin(theta) * ringRadius / radius,
                y / radius,
                std::cos(theta) * ringRadius / radius
            );
            const glm::vec3 position = normal * radius;
            const glm::vec2 uv(
                static_cast<float>(slice) / static_cast<float>(slices),
                static_cast<float>(stack) / static_cast<float>(stacks)
            );
            const glm::vec3 tangent(
                std::cos(theta),
                0.0f,
                -std::sin(theta)
            );
            AppendVertex(data, position, normal, uv, tangent);
        }
    }
    for (int stack = 0; stack < stacks; ++stack)
    {
        for (int slice = 0; slice < slices; ++slice)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(
                stack * (slices + 1) + slice
            );
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = a + static_cast<std::uint32_t>(slices) + 1U;
            const std::uint32_t d = c + 1U;
            AppendTriangle(data, a, c, b);
            AppendTriangle(data, b, c, d);
        }
    }
    return data;
}

DefaultModelData BuildCylinderMeshData(
    float radius,
    float height,
    int segments
)
{
    if (!std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || height <= 0.0f || segments < 3)
    {
        throw std::invalid_argument(
            "Cylinder requires radius > 0, height > 0, segments >= 3"
        );
    }
    DefaultModelData data;
    ConfigureLayout(data);
    const float half = height * 0.5f;
    const float pi2 = 2.0f * 3.14159265358979f;

    // Side wall.
    const std::uint32_t sideStart = static_cast<std::uint32_t>(
        data.vertices.size() / PrimitiveStride
    );
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle = static_cast<float>(segment) /
            static_cast<float>(segments) * pi2;
        const glm::vec3 normal(std::sin(angle), 0.0f, std::cos(angle));
        const glm::vec3 tangent(std::cos(angle), 0.0f, -std::sin(angle));
        for (int row = 0; row <= 1; ++row)
        {
            const float y = row == 0 ? -half : half;
            const glm::vec2 uv(
                static_cast<float>(segment) / static_cast<float>(segments),
                static_cast<float>(row)
            );
            AppendVertex(
                data,
                normal * radius + glm::vec3(0.0f, y, 0.0f),
                normal,
                uv,
                tangent
            );
        }
    }
    for (int segment = 0; segment < segments; ++segment)
    {
        const std::uint32_t base = sideStart +
            static_cast<std::uint32_t>(segment * 2);
        AppendTriangle(data, base, base + 1U, base + 2U);
        AppendTriangle(data, base + 1U, base + 3U, base + 2U);
    }

    // Caps: top (+Y) and bottom (-Y), wound to face outward.
    const auto addCap = [&](float capY, float normalY, float uvV)
    {
        const std::uint32_t center = static_cast<std::uint32_t>(
            data.vertices.size() / PrimitiveStride
        );
        AppendVertex(
            data,
            glm::vec3(0.0f, capY, 0.0f),
            glm::vec3(0.0f, normalY, 0.0f),
            glm::vec2(0.5f, uvV),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
        for (int segment = 0; segment <= segments; ++segment)
        {
            const float angle = static_cast<float>(segment) /
                static_cast<float>(segments) * pi2;
            const glm::vec2 uv(
                0.5f + 0.5f * std::sin(angle),
                uvV + 0.5f * std::cos(angle)
            );
            AppendVertex(
                data,
                glm::vec3(std::sin(angle) * radius, capY, std::cos(angle) * radius),
                glm::vec3(0.0f, normalY, 0.0f),
                uv,
                glm::vec3(1.0f, 0.0f, 0.0f)
            );
        }
        for (int segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t base = center + 1U +
                static_cast<std::uint32_t>(segment);
            if (normalY > 0.0f)
            {
                AppendTriangle(data, center, base + 1U, base);
            }
            else
            {
                AppendTriangle(data, center, base, base + 1U);
            }
        }
    };
    addCap(half, 1.0f, 0.0f);
    addCap(-half, -1.0f, 1.0f);
    return data;
}

DefaultModelData BuildCapsuleMeshData(
    float radius,
    float height,
    int segments
)
{
    if (!std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || height <= 0.0f || segments < 3)
    {
        throw std::invalid_argument(
            "Capsule requires radius > 0, height > 0, segments >= 3"
        );
    }
    DefaultModelData data;
    ConfigureLayout(data);
    const float half = height * 0.5f;
    const float pi = 3.14159265358979f;
    const int latitudeRings = 8;

    for (int ring = 0; ring <= latitudeRings * 2; ++ring)
    {
        const float phi = static_cast<float>(ring) /
            static_cast<float>(latitudeRings * 2) * pi;
        const float y = radius * std::cos(phi);
        const float ringRadius = radius * std::sin(phi);
        const float capY = std::max(-half, std::min(half, y));
        for (int segment = 0; segment <= segments; ++segment)
        {
            const float theta = static_cast<float>(segment) /
                static_cast<float>(segments) * 2.0f * pi;
            const glm::vec3 normal(
                std::sin(theta) * ringRadius / radius,
                y / radius,
                std::cos(theta) * ringRadius / radius
            );
            const glm::vec3 position(
                normal.x * radius,
                capY + (y - capY),
                normal.z * radius
            );
            const glm::vec2 uv(
                static_cast<float>(segment) / static_cast<float>(segments),
                static_cast<float>(ring) /
                    static_cast<float>(latitudeRings * 2)
            );
            const glm::vec3 tangent(std::cos(theta), 0.0f, -std::sin(theta));
            AppendVertex(data, position, normal, uv, tangent);
        }
    }
    for (int ring = 0; ring < latitudeRings * 2; ++ring)
    {
        for (int segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(
                ring * (segments + 1) + segment
            );
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = a + static_cast<std::uint32_t>(segments) + 1U;
            const std::uint32_t d = c + 1U;
            AppendTriangle(data, a, c, b);
            AppendTriangle(data, b, c, d);
        }
    }
    return data;
}

DefaultModelData BuildConeMeshData(
    float radius,
    float height,
    int segments
)
{
    if (!std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || height <= 0.0f || segments < 3)
    {
        throw std::invalid_argument(
            "Cone requires radius > 0, height > 0, segments >= 3"
        );
    }
    DefaultModelData data;
    ConfigureLayout(data);
    const float half = height * 0.5f;
    const float pi2 = 2.0f * 3.14159265358979f;
    const float sideNormalY = radius / std::sqrt(
        radius * radius + height * height
    );
    const float sideNormalRadial = height / std::sqrt(
        radius * radius + height * height
    );

    // Apex.
    const std::uint32_t apex = static_cast<std::uint32_t>(
        data.vertices.size() / PrimitiveStride
    );
    AppendVertex(
        data,
        glm::vec3(0.0f, half, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec2(0.5f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    const std::uint32_t ringStart = static_cast<std::uint32_t>(
        data.vertices.size() / PrimitiveStride
    );
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle = static_cast<float>(segment) /
            static_cast<float>(segments) * pi2;
        const glm::vec3 position(
            std::sin(angle) * radius,
            -half,
            std::cos(angle) * radius
        );
        const glm::vec3 normal(
            std::sin(angle) * sideNormalRadial,
            sideNormalY,
            std::cos(angle) * sideNormalRadial
        );
        const glm::vec2 uv(
            static_cast<float>(segment) / static_cast<float>(segments),
            0.0f
        );
        const glm::vec3 tangent(std::cos(angle), 0.0f, -std::sin(angle));
        AppendVertex(data, position, normal, uv, tangent);
    }
    for (int segment = 0; segment < segments; ++segment)
    {
        const std::uint32_t base = ringStart +
            static_cast<std::uint32_t>(segment);
        AppendTriangle(data, apex, base + 1U, base);
    }

    // Base disk, facing downward.
    const std::uint32_t center = static_cast<std::uint32_t>(
        data.vertices.size() / PrimitiveStride
    );
    AppendVertex(
        data,
        glm::vec3(0.0f, -half, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec2(0.5f, 0.5f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle = static_cast<float>(segment) /
            static_cast<float>(segments) * pi2;
        const glm::vec2 uv(
            0.5f + 0.5f * std::sin(angle),
            0.5f + 0.5f * std::cos(angle)
        );
        AppendVertex(
            data,
            glm::vec3(std::sin(angle) * radius, -half, std::cos(angle) * radius),
            glm::vec3(0.0f, -1.0f, 0.0f),
            uv,
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
    }
    for (int segment = 0; segment < segments; ++segment)
    {
        const std::uint32_t base = center + 1U +
            static_cast<std::uint32_t>(segment);
        AppendTriangle(data, center, base, base + 1U);
    }
    return data;
}

DefaultModelData BuildTorusMeshData(
    float majorRadius,
    float minorRadius,
    int majorSegments,
    int minorSegments
)
{
    if (!std::isfinite(majorRadius) || majorRadius <= 0.0f ||
        !std::isfinite(minorRadius) || minorRadius <= 0.0f ||
        majorSegments < 3 || minorSegments < 3)
    {
        throw std::invalid_argument(
            "Torus requires radii > 0 and segments >= 3"
        );
    }
    DefaultModelData data;
    ConfigureLayout(data);
    const float pi2 = 2.0f * 3.14159265358979f;
    for (int major = 0; major <= majorSegments; ++major)
    {
        const float u = static_cast<float>(major) /
            static_cast<float>(majorSegments) * pi2;
        const float cosU = std::cos(u);
        const float sinU = std::sin(u);
        for (int minor = 0; minor <= minorSegments; ++minor)
        {
            const float v = static_cast<float>(minor) /
                static_cast<float>(minorSegments) * pi2;
            const float cosV = std::cos(v);
            const float sinV = std::sin(v);
            const glm::vec3 normal(cosV * cosU, sinV, cosV * sinU);
            const glm::vec3 position(
                (majorRadius + minorRadius * cosV) * cosU,
                minorRadius * sinV,
                (majorRadius + minorRadius * cosV) * sinU
            );
            const glm::vec2 uv(
                static_cast<float>(major) / static_cast<float>(majorSegments),
                static_cast<float>(minor) / static_cast<float>(minorSegments)
            );
            const glm::vec3 tangent(-sinU, 0.0f, cosU);
            AppendVertex(data, position, normal, uv, tangent);
        }
    }
    for (int major = 0; major < majorSegments; ++major)
    {
        for (int minor = 0; minor < minorSegments; ++minor)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(
                major * (minorSegments + 1) + minor
            );
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = a + static_cast<std::uint32_t>(minorSegments) + 1U;
            const std::uint32_t d = c + 1U;
            AppendTriangle(data, a, c, b);
            AppendTriangle(data, b, c, d);
        }
    }
    return data;
}
}  // namespace wisteria
