#version 330 core

in vec3 localPosition;
out vec4 outputColor;

uniform samplerCube environmentMap;
uniform float roughness;

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

float DistributionGgx(float normalDotHalf, float surfaceRoughness)
{
    float alpha = surfaceRoughness * surfaceRoughness;
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf *
        (alphaSquared - 1.0) + 1.0;
    return alphaSquared /
        max(PI * denominator * denominator, 0.000001);
}

vec3 ImportanceSampleGgx(
    vec2 samplePoint,
    vec3 normal,
    float surfaceRoughness
)
{
    float alpha = surfaceRoughness * surfaceRoughness;
    float phi = 2.0 * PI * samplePoint.x;
    float cosineTheta = sqrt(
        (1.0 - samplePoint.y) /
        max(1.0 + (alpha * alpha - 1.0) * samplePoint.y, 0.000001)
    );
    float sineTheta = sqrt(max(1.0 - cosineTheta * cosineTheta, 0.0));
    vec3 halfDirection = vec3(
        cos(phi) * sineTheta,
        sin(phi) * sineTheta,
        cosineTheta
    );

    vec3 reference = abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0)
        : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(reference, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(
        tangent * halfDirection.x +
        bitangent * halfDirection.y +
        normal * halfDirection.z
    );
}

void main()
{
    vec3 normal = normalize(localPosition);
    vec3 reflection = normal;
    vec3 viewDirection = reflection;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    float sourceResolution = float(textureSize(environmentMap, 0).x);
    float texelSolidAngle = 4.0 * PI /
        (6.0 * sourceResolution * sourceResolution);

    for (uint index = 0u; index < SAMPLE_COUNT; ++index)
    {
        vec3 halfDirection = ImportanceSampleGgx(
            Hammersley(index),
            normal,
            max(roughness, 0.001)
        );
        vec3 lightDirection = normalize(
            2.0 * dot(viewDirection, halfDirection) * halfDirection -
            viewDirection
        );
        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        if (normalDotLight <= 0.0)
            continue;

        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        float distribution = DistributionGgx(
            normalDotHalf,
            max(roughness, 0.001)
        );
        float probabilityDensity = distribution * normalDotHalf /
            max(4.0 * halfDotView, 0.000001);
        float sampleSolidAngle = 1.0 /
            (float(SAMPLE_COUNT) * probabilityDensity + 0.000001);
        float mipLevel = roughness <= 0.001
            ? 0.0
            : 0.5 * log2(sampleSolidAngle / texelSolidAngle);

        prefilteredColor += textureLod(
            environmentMap,
            lightDirection,
            max(mipLevel, 0.0)
        ).rgb * normalDotLight;
        totalWeight += normalDotLight;
    }

    prefilteredColor /= max(totalWeight, 0.000001);
    outputColor = vec4(prefilteredColor, 1.0);
}
