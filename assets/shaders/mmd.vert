#version 330 core

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;
layout(location = 5) in vec2 additionalTexCoord;
layout(location = 6) in float edgeScale;
layout(location = 7) in vec4 boneIndices;
layout(location = 8) in vec4 boneWeights;
layout(location = 9) in vec3 morphPositionOffset;
layout(location = 10) in vec4 morphUvOffset;
layout(location = 11) in vec4 morphAdditionalUv1Offset;
layout(location = 12) in vec4 morphAdditionalUv2Offset;
layout(location = 13) in vec4 morphAdditionalUv3Offset;
layout(location = 14) in vec4 morphAdditionalUv4Offset;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightViewProjection[4];
uniform int outlinePass;
uniform float materialEdgeSize;
uniform int skinningEnabled;
uniform samplerBuffer boneMatrixPalette;

out vec2 vertexTexCoord;
out vec3 fragmentPosition;
out vec3 fragmentNormal;
out vec3 fragmentViewNormal;
out vec2 fragmentAdditionalTexCoord;
out vec4 fragmentShadowCoord[4];
out float fragmentViewDepth;

mat4 BoneMatrix(int boneIndex)
{
    int firstTexel = boneIndex * 4;
    return mat4(
        texelFetch(boneMatrixPalette, firstTexel),
        texelFetch(boneMatrixPalette, firstTexel + 1),
        texelFetch(boneMatrixPalette, firstTexel + 2),
        texelFetch(boneMatrixPalette, firstTexel + 3)
    );
}

mat4 SkinMatrix()
{
    if (skinningEnabled == 0)
        return mat4(1.0);

    ivec4 indices = ivec4(boneIndices + vec4(0.5));
    return BoneMatrix(indices.x) * boneWeights.x +
        BoneMatrix(indices.y) * boneWeights.y +
        BoneMatrix(indices.z) * boneWeights.z +
        BoneMatrix(indices.w) * boneWeights.w;
}

void main()
{
    mat4 skinMatrix = SkinMatrix();
    vec4 localPosition = skinMatrix * vec4(position + morphPositionOffset, 1.0);
    vec3 localNormal = mat3(skinMatrix) * normal;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 worldNormal = normalize(normalMatrix * localNormal);
    vec4 worldPosition = model * localPosition;
    fragmentViewDepth = -(view * worldPosition).z;
    fragmentShadowCoord[0] = lightViewProjection[0] * worldPosition;
    fragmentShadowCoord[1] = lightViewProjection[1] * worldPosition;
    fragmentShadowCoord[2] = lightViewProjection[2] * worldPosition;
    fragmentShadowCoord[3] = lightViewProjection[3] * worldPosition;

    if (outlinePass != 0)
    {
        worldPosition.xyz += worldNormal * materialEdgeSize *
            max(edgeScale, 0.0) * 0.0025;
    }

    gl_Position = projection * view * worldPosition;
    vertexTexCoord = texCoord + morphUvOffset.xy;
    fragmentPosition = worldPosition.xyz;
    fragmentNormal = worldNormal;
    fragmentViewNormal = normalize(mat3(view) * worldNormal);
    fragmentAdditionalTexCoord =
        additionalTexCoord + morphAdditionalUv1Offset.xy;
}
