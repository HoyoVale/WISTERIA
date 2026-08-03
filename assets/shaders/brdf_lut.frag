#version 330 core

in vec2 texCoord;
out vec2 outputColor;

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

vec3 ImportanceSampleGgx(vec2 samplePoint, float roughness)
{
    float alpha = roughness * roughness;
    float phi = 2.0 * PI * samplePoint.x;
    float cosineTheta = sqrt(
        (1.0 - samplePoint.y) /
        max(1.0 + (alpha * alpha - 1.0) * samplePoint.y, 0.000001)
    );
    float sineTheta = sqrt(max(1.0 - cosineTheta * cosineTheta, 0.0));
    return vec3(
        cos(phi) * sineTheta,
        sin(phi) * sineTheta,
        cosineTheta
    );
}

float GeometrySchlickGgx(float normalDotDirection, float roughness)
{
    float k = roughness * roughness * 0.5;
    return normalDotDirection /
        max(normalDotDirection * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(
    float normalDotView,
    float normalDotLight,
    float roughness
)
{
    return GeometrySchlickGgx(normalDotView, roughness) *
        GeometrySchlickGgx(normalDotLight, roughness);
}

vec2 IntegrateBrdf(float normalDotView, float roughness)
{
    vec3 viewDirection = vec3(
        sqrt(max(1.0 - normalDotView * normalDotView, 0.0)),
        0.0,
        normalDotView
    );
    vec3 normal = vec3(0.0, 0.0, 1.0);
    float scale = 0.0;
    float bias = 0.0;

    for (uint index = 0u; index < SAMPLE_COUNT; ++index)
    {
        vec3 halfDirection = ImportanceSampleGgx(
            Hammersley(index),
            max(roughness, 0.001)
        );
        vec3 lightDirection = normalize(
            2.0 * dot(viewDirection, halfDirection) * halfDirection -
            viewDirection
        );
        float normalDotLight = max(lightDirection.z, 0.0);
        float normalDotHalf = max(halfDirection.z, 0.0);
        float viewDotHalf = max(dot(viewDirection, halfDirection), 0.0);
        if (normalDotLight <= 0.0)
            continue;

        float geometry = GeometrySmith(
            normalDotView,
            normalDotLight,
            roughness
        );
        float visibility = geometry * viewDotHalf /
            max(normalDotHalf * normalDotView, 0.000001);
        float fresnel = pow(1.0 - viewDotHalf, 5.0);
        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }
    return vec2(scale, bias) / float(SAMPLE_COUNT);
}

void main()
{
    outputColor = IntegrateBrdf(texCoord.x, texCoord.y);
}
