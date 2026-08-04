#version 330 core

in vec2 vertexTexCoord;
in vec3 fragmentPosition;
in vec3 fragmentNormal;
in vec3 fragmentViewNormal;
in vec2 fragmentAdditionalTexCoord;
in vec4 fragmentShadowCoord[4];
in float fragmentViewDepth;

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

const int MAX_POINT_LIGHTS = 8;
const int MAX_DIRECTIONAL_LIGHTS = 4;
const int MAX_SPOT_LIGHTS = 4;

uniform sampler2D baseColorTexture;
uniform sampler2D sphereTexture;
uniform sampler2D toonTexture;

uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int pointLightCount;
uniform DirectionalLight directionalLights[MAX_DIRECTIONAL_LIGHTS];
uniform int directionalLightCount;
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];
uniform int spotLightCount;

uniform vec3 cameraPosition;
uniform float ambientStrength;
uniform vec4 materialBaseColorFactor;
uniform vec3 materialSpecularColor;
uniform float materialShininess;
uniform vec3 materialAmbientColor;
uniform int materialAlphaMode;
uniform float materialAlphaCutoff;
uniform int oitPass;
uniform int hasBaseTexture;
uniform int hasSphereTexture;
uniform int sphereMapMode;
uniform int hasToonTexture;
uniform int outlinePass;
uniform vec4 materialEdgeColor;
uniform float materialEdgeSize;
uniform vec4 materialTextureFactor;
uniform vec4 materialSphereTextureFactor;
uniform vec4 materialToonTextureFactor;
uniform sampler2DArray shadowMap;
uniform int shadowEnabled;
uniform int receiveShadow;
uniform vec2 shadowMapSize;
uniform float shadowSplitPositions[5];

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

float Attenuation(
    float range,
    float constant,
    float linear,
    float quadratic,
    float distanceToLight
)
{
    float attenuation = 1.0 / max(
        constant + linear * distanceToLight +
        quadratic * distanceToLight * distanceToLight,
        0.0001
    );
    return attenuation *
        (1.0 - smoothstep(range * 0.8, range, distanceToLight));
}

float ShadowFactor()
{
    if (shadowEnabled == 0 || receiveShadow == 0)
        return 1.0;

    int cascade = 3;
    for (int index = 0; index < 4; ++index)
    {
        if (fragmentViewDepth <= shadowSplitPositions[index + 1])
        {
            cascade = index;
            break;
        }
    }

    vec3 projected =
        fragmentShadowCoord[cascade].xyz / fragmentShadowCoord[cascade].w;
    vec2 shadowUv = projected.xy * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0)
    {
        return 1.0;
    }
    float currentDepth = projected.z * 0.5 + 0.5;
    if (currentDepth > 1.0)
        return 1.0;

    float bias = 0.003;
    float visibility = 0.0;
    vec2 texelSize = 1.0 / shadowMapSize;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float shadowDepth = texture(
                shadowMap,
                vec3(shadowUv + vec2(x, y) * texelSize, float(cascade))
            ).r;
            visibility += (currentDepth - bias) <= shadowDepth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

vec3 ToonFactor(float normalDotLight)
{
    if (hasToonTexture == 0)
        return vec3(max(normalDotLight, 0.0));

    float coordinate = clamp(normalDotLight * 0.5 + 0.5, 0.0, 1.0);
    return (texture(toonTexture, vec2(0.5, coordinate)) *
        materialToonTextureFactor).rgb;
}

vec3 DirectLight(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance,
    vec3 albedo
)
{
    float normalDotLight = dot(normal, lightDirection);
    vec3 diffuse = albedo * ToonFactor(normalDotLight);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float specularAmount = normalDotLight > 0.0
        ? pow(
            max(dot(normal, halfDirection), 0.0),
            max(materialShininess, 1.0)
        )
        : 0.0;
    vec3 specular = materialSpecularColor * specularAmount;
    return (diffuse + specular) * radiance;
}

vec3 PointContribution(
    PointLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo
)
{
    vec3 offset = light.position - fragmentPosition;
    float distanceToLight = length(offset);
    vec3 lightDirection = offset / max(distanceToLight, 0.0001);
    float attenuation = Attenuation(
        light.range,
        light.constant,
        light.linear,
        light.quadratic,
        distanceToLight
    );
    return DirectLight(
        normal,
        viewDirection,
        lightDirection,
        light.radiance * attenuation,
        albedo
    );
}

vec3 DirectionalContribution(
    DirectionalLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo,
    float shadowFactor
)
{
    return DirectLight(
        normal,
        viewDirection,
        normalize(-light.direction),
        light.radiance * shadowFactor,
        albedo
    );
}

vec3 SpotContribution(
    SpotLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 albedo
)
{
    vec3 offset = light.position - fragmentPosition;
    float distanceToLight = length(offset);
    vec3 lightDirection = offset / max(distanceToLight, 0.0001);
    float theta = dot(-lightDirection, normalize(light.direction));
    float cone = clamp(
        (theta - light.outerCutoff) /
        max(light.innerCutoff - light.outerCutoff, 0.0001),
        0.0,
        1.0
    );
    float attenuation = Attenuation(
        light.range,
        light.constant,
        light.linear,
        light.quadratic,
        distanceToLight
    );
    return DirectLight(
        normal,
        viewDirection,
        lightDirection,
        light.radiance * attenuation * cone,
        albedo
    );
}

void main()
{
    if (outlinePass != 0)
    {
        vec3 edge = pow(
            max(materialEdgeColor.rgb, vec3(0.0)),
            vec3(1.0 / 2.2)
        );
        WriteOutput(vec4(edge, materialEdgeColor.a));
        return;
    }

    vec4 sampledColor = hasBaseTexture != 0
        ? texture(baseColorTexture, vertexTexCoord) * materialTextureFactor
        : vec4(1.0);
    vec4 baseColor = sampledColor * materialBaseColorFactor;
    if (materialAlphaMode == 0)
        baseColor.a = 1.0;
    else if (materialAlphaMode == 1 && baseColor.a < materialAlphaCutoff)
        discard;
    else if (materialAlphaMode == 2 && baseColor.a <= 0.001)
        discard;

    vec3 normal = normalize(fragmentNormal) *
        (gl_FrontFacing ? 1.0 : -1.0);
    vec3 viewDirection = normalize(cameraPosition - fragmentPosition);
    vec3 albedo = baseColor.rgb;
    vec3 sphereAddition = vec3(0.0);
    if (hasSphereTexture != 0 && sphereMapMode != 0)
    {
        vec2 sphereUv;
        if (sphereMapMode == 3)
            sphereUv = fragmentAdditionalTexCoord;
        else
        {
            sphereUv = fragmentViewNormal.xy * 0.5 + 0.5;
            sphereUv.y = 1.0 - sphereUv.y;
        }
        vec3 sphere = (texture(sphereTexture, sphereUv) *
            materialSphereTextureFactor).rgb;
        if (sphereMapMode == 1 || sphereMapMode == 3)
            albedo *= sphere;
        else if (sphereMapMode == 2)
            sphereAddition = sphere;
    }

    vec3 color = albedo * (materialAmbientColor + vec3(ambientStrength));
    float shadowFactor = ShadowFactor();
    for (int index = 0; index < MAX_POINT_LIGHTS; ++index)
    {
        if (index >= pointLightCount)
            break;
        color += PointContribution(
            pointLights[index], normal, viewDirection, albedo
        );
    }
    for (int index = 0; index < MAX_DIRECTIONAL_LIGHTS; ++index)
    {
        if (index >= directionalLightCount)
            break;
        color += DirectionalContribution(
            directionalLights[index],
            normal,
            viewDirection,
            albedo,
            index == 0 ? shadowFactor : 1.0
        );
    }
    for (int index = 0; index < MAX_SPOT_LIGHTS; ++index)
    {
        if (index >= spotLightCount)
            break;
        color += SpotContribution(
            spotLights[index], normal, viewDirection, albedo
        );
    }

    color += sphereAddition;
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    WriteOutput(vec4(color, baseColor.a));
}
