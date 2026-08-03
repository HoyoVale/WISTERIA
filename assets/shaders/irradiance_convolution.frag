#version 330 core

in vec3 localPosition;
out vec4 outputColor;

uniform samplerCube environmentMap;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 256u;

float RadicalInverseVanDerCorput(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) |
           ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) |
           ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) |
           ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) |
           ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint index)
{
    return vec2(
        float(index) / float(SAMPLE_COUNT),
        RadicalInverseVanDerCorput(index)
    );
}

vec3 CosineHemisphere(vec2 samplePoint, vec3 normal)
{
    float radius = sqrt(samplePoint.x);
    float phi = 2.0 * PI * samplePoint.y;
    vec3 localDirection = vec3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(1.0 - samplePoint.x, 0.0))
    );

    vec3 reference = abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0)
        : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(reference, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(
        tangent * localDirection.x +
        bitangent * localDirection.y +
        normal * localDirection.z
    );
}

void main()
{
    vec3 normal = normalize(localPosition);
    vec3 irradiance = vec3(0.0);
    for (uint index = 0u; index < SAMPLE_COUNT; ++index)
    {
        vec3 sampleDirection = CosineHemisphere(
            Hammersley(index),
            normal
        );
        irradiance += texture(environmentMap, sampleDirection).rgb;
    }
    irradiance *= PI / float(SAMPLE_COUNT);
    outputColor = vec4(irradiance, 1.0);
}
