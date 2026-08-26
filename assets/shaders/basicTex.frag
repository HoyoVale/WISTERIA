#version 330

in vec2 vertexTexCoord;
in vec3 fragmentPosition;
in vec3 fragmentNormal;
in vec4 fragmentTangent;

struct PointLight
{
    vec3 position;
    vec3 radiance;
    float range;
    float constant;
    float linear;
    float quadratic;
};

struct DirectionalLight
{
    vec3 direction;
    vec3 radiance;
};

struct SpotLight
{
    vec3 position;
    vec3 direction;
    vec3 radiance;
    float range;
    float constant;
    float linear;
    float quadratic;
    float innerCutoff;
    float outerCutoff;
};

const float PI = 3.14159265359;
const int MAX_POINT_LIGHTS = 8;
const int MAX_DIRECTIONAL_LIGHTS = 4;
const int MAX_SPOT_LIGHTS = 4;

uniform sampler2D baseColorTexture;
uniform sampler2D normalTexture;
uniform sampler2D metallicRoughnessTexture;
uniform sampler2D emissiveTexture;
uniform sampler2D occlusionTexture;
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLut;

uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int pointLightCount;
uniform DirectionalLight directionalLights[MAX_DIRECTIONAL_LIGHTS];
uniform int directionalLightCount;
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];
uniform int spotLightCount;

uniform float ambientStrength;
uniform int toneMappingMode;
uniform float exposure;
uniform vec3 cameraPosition;
uniform vec4 materialBaseColorFactor;
uniform float materialMetallicFactor;
uniform float materialRoughnessFactor;
uniform float materialNormalScale;
uniform vec3 materialEmissiveFactor;
uniform float materialOcclusionStrength;
uniform int materialAlphaMode;
uniform float materialAlphaCutoff;
uniform int oitPass;

uniform int hasBaseTexture;
uniform int hasNormalTexture;
uniform int hasMetallicRoughnessTexture;
uniform int hasEmissiveTexture;
uniform int hasOcclusionTexture;
uniform int hasEnvironment;
uniform float environmentIntensity;
uniform float maxReflectionLod;

layout(location = 0) out vec4 outputColor;
layout(location = 1) out float oitRevealage;

void WriteOutput(vec4 color)
{
    if (oitPass == 0)
    {
        outputColor = color;
        oitRevealage = 0.0;
        return;
    }

    float alpha = clamp(color.a, 0.0, 1.0);
    if (oitPass == 2)
    {
        // OpenGL 3.3 fallback: output alpha through location zero during
        // the dedicated revealage pass.
        outputColor = vec4(alpha);
        oitRevealage = alpha;
        return;
    }

    float weight = clamp(
        pow(min(1.0, alpha * 10.0) + 0.01, 3.0) *
        1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
        1e-2,
        3e3
    );
    outputColor = vec4(color.rgb * alpha, alpha) * weight;
    oitRevealage = alpha;
}

vec3 CalculateSurfaceNormal()
{
    float faceSign = gl_FrontFacing ? 1.0 : -1.0;
    vec3 normal = normalize(fragmentNormal) * faceSign;
    if (hasNormalTexture == 0)
        return normal;

    vec3 tangent = normalize(
        fragmentTangent.xyz -
        normal * dot(normal, fragmentTangent.xyz)
    );
    vec3 bitangent =
        normalize(cross(normal, tangent)) * fragmentTangent.w;
    vec3 tangentNormal =
        texture(normalTexture, vertexTexCoord).rgb * 2.0 - 1.0;
    tangentNormal.xy *= materialNormalScale;

    return normalize(mat3(tangent, bitangent, normal) * tangentNormal);
}

float DistributionGgx(vec3 normal, vec3 halfDirection, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalf = max(dot(normal, halfDirection), 0.0);
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared *
        (alphaSquared - 1.0) + 1.0;

    return alphaSquared /
        max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGgx(float normalDotDirection, float roughness)
{
    float value = roughness + 1.0;
    float k = value * value / 8.0;
    return normalDotDirection /
        max(normalDotDirection * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    float roughness
)
{
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    return GeometrySchlickGgx(normalDotView, roughness) *
        GeometrySchlickGgx(normalDotLight, roughness);
}

vec3 FresnelSchlick(float cosine, vec3 reflectanceAtNormalIncidence)
{
    return reflectanceAtNormalIncidence +
        (1.0 - reflectanceAtNormalIncidence) *
        pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(
    float cosine,
    vec3 reflectanceAtNormalIncidence,
    float roughness
)
{
    return reflectanceAtNormalIncidence +
        (max(vec3(1.0 - roughness), reflectanceAtNormalIncidence) -
            reflectanceAtNormalIncidence) *
        pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 CalculateDirectLight(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance,
    vec3 albedo,
    float metallic,
    float roughness
)
{
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    if (normalDotLight <= 0.0 || normalDotView <= 0.0)
        return vec3(0.0);

    vec3 halfDirection = normalize(viewDirection + lightDirection);
    vec3 baseReflectance = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(
        max(dot(halfDirection, viewDirection), 0.0),
        baseReflectance
    );
    float distribution = DistributionGgx(
        normal,
        halfDirection,
        roughness
    );
    float geometry = GeometrySmith(
        normal,
        viewDirection,
        lightDirection,
        roughness
    );

    vec3 specular =
        distribution * geometry * fresnel /
        max(4.0 * normalDotView * normalDotLight, 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 diffuse = diffuseWeight * albedo / PI;

    return (diffuse + specular) * radiance * normalDotLight;
}

float CalculateAttenuation(
    float range,
    float constant,
    float linear,
    float quadratic,
    float distanceToLight
)
{
    float attenuation = 1.0 / max(
        constant +
        linear * distanceToLight +
        quadratic * distanceToLight * distanceToLight,
        0.0001
    );
    float rangeFade = 1.0 - smoothstep(
        range * 0.8,
        range,
        distanceToLight
    );
    return attenuation * rangeFade;
}

vec3 CalculatePointLight(
    PointLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo,
    float metallic,
    float roughness
)
{
    vec3 lightOffset = light.position - fragmentPosition;
    float lightDistance = length(lightOffset);
    vec3 lightDirection = lightOffset / max(lightDistance, 0.0001);
    float attenuation = CalculateAttenuation(
        light.range,
        light.constant,
        light.linear,
        light.quadratic,
        lightDistance
    );
    return CalculateDirectLight(
        normal,
        viewDirection,
        lightDirection,
        light.radiance * attenuation,
        albedo,
        metallic,
        roughness
    );
}

vec3 CalculateDirectionalLight(
    DirectionalLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo,
    float metallic,
    float roughness
)
{
    return CalculateDirectLight(
        normal,
        viewDirection,
        normalize(-light.direction),
        light.radiance,
        albedo,
        metallic,
        roughness
    );
}

vec3 CalculateSpotLight(
    SpotLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo,
    float metallic,
    float roughness
)
{
    vec3 lightOffset = light.position - fragmentPosition;
    float lightDistance = length(lightOffset);
    vec3 lightDirection = lightOffset / max(lightDistance, 0.0001);
    float theta = dot(-lightDirection, normalize(light.direction));
    float coneFactor = clamp(
        (theta - light.outerCutoff) /
            max(light.innerCutoff - light.outerCutoff, 0.0001),
        0.0,
        1.0
    );
    float attenuation = CalculateAttenuation(
        light.range,
        light.constant,
        light.linear,
        light.quadratic,
        lightDistance
    );
    return CalculateDirectLight(
        normal,
        viewDirection,
        lightDirection,
        light.radiance * attenuation * coneFactor,
        albedo,
        metallic,
        roughness
    );
}


vec3 AcesTonemap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(
        color * (a * color + b) / (color * (c * color + d) + e),
        0.0,
        1.0
    );
}

void main()
{
    vec4 sampledColor = hasBaseTexture != 0
        ? texture(baseColorTexture, vertexTexCoord)
        : vec4(1.0);
    vec4 baseColor = sampledColor * materialBaseColorFactor;

    if (materialAlphaMode == 0)
        baseColor.a = 1.0;
    else if (materialAlphaMode == 1 && baseColor.a < materialAlphaCutoff)
        discard;
    else if (materialAlphaMode == 2 && baseColor.a <= 0.001)
        discard;

    vec4 metallicRoughnessSample = hasMetallicRoughnessTexture != 0
        ? texture(metallicRoughnessTexture, vertexTexCoord)
        : vec4(1.0);
    float metallic = clamp(
        materialMetallicFactor * metallicRoughnessSample.b,
        0.0,
        1.0
    );
    float roughness = clamp(
        materialRoughnessFactor * metallicRoughnessSample.g,
        0.045,
        1.0
    );

    float sampledOcclusion = hasOcclusionTexture != 0
        ? texture(occlusionTexture, vertexTexCoord).r
        : 1.0;
    float occlusion = mix(
        1.0,
        sampledOcclusion,
        materialOcclusionStrength
    );
    vec3 emissiveSample = hasEmissiveTexture != 0
        ? texture(emissiveTexture, vertexTexCoord).rgb
        : vec3(1.0);
    vec3 emissive = emissiveSample * materialEmissiveFactor;

    vec3 normal = CalculateSurfaceNormal();
    vec3 viewDirection = normalize(cameraPosition - fragmentPosition);
    vec3 directLighting = vec3(0.0);

    for (int index = 0; index < MAX_POINT_LIGHTS; ++index)
    {
        if (index >= pointLightCount)
            break;
        directLighting += CalculatePointLight(
            pointLights[index],
            normal,
            viewDirection,
            baseColor.rgb,
            metallic,
            roughness
        );
    }
    for (int index = 0; index < MAX_DIRECTIONAL_LIGHTS; ++index)
    {
        if (index >= directionalLightCount)
            break;
        directLighting += CalculateDirectionalLight(
            directionalLights[index],
            normal,
            viewDirection,
            baseColor.rgb,
            metallic,
            roughness
        );
    }
    for (int index = 0; index < MAX_SPOT_LIGHTS; ++index)
    {
        if (index >= spotLightCount)
            break;
        directLighting += CalculateSpotLight(
            spotLights[index],
            normal,
            viewDirection,
            baseColor.rgb,
            metallic,
            roughness
        );
    }

    vec3 ambient;
    if (hasEnvironment != 0)
    {
        float normalDotView = max(dot(normal, viewDirection), 0.0);
        vec3 baseReflectance = mix(vec3(0.04), baseColor.rgb, metallic);
        vec3 fresnel = FresnelSchlickRoughness(
            normalDotView,
            baseReflectance,
            roughness
        );
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 irradiance = texture(irradianceMap, normal).rgb;
        vec3 diffuseEnvironment = irradiance * baseColor.rgb;

        vec3 reflectionDirection = reflect(-viewDirection, normal);
        vec3 prefilteredEnvironment = textureLod(
            prefilterMap,
            reflectionDirection,
            roughness * maxReflectionLod
        ).rgb;
        vec2 integratedBrdf = texture(
            brdfLut,
            vec2(normalDotView, roughness)
        ).rg;
        vec3 specularEnvironment = prefilteredEnvironment *
            (fresnel * integratedBrdf.x + integratedBrdf.y);

        ambient = (diffuseWeight * diffuseEnvironment +
            specularEnvironment) * occlusion * environmentIntensity;
    }
    else
    {
        ambient = ambientStrength * baseColor.rgb * occlusion;
    }
    vec3 color = ambient + directLighting + emissive;
    color *= exposure;
    color = toneMappingMode == 0
        ? color / (color + vec3(1.0))
        : AcesTonemap(color);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    WriteOutput(vec4(color, baseColor.a));
}
