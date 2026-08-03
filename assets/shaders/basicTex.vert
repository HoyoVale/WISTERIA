#version 330

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;
layout(location = 4) in vec4 tangent;
layout(location = 7) in vec4 boneIndices;
layout(location = 8) in vec4 boneWeights;
layout(location = 9) in vec3 morphPositionOffset;
layout(location = 10) in vec4 morphUvOffset;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int skinningEnabled;
uniform samplerBuffer boneMatrixPalette;

out vec2 vertexTexCoord;
out vec3 fragmentPosition;
out vec3 fragmentNormal;
out vec4 fragmentTangent;

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
    vec3 localTangent = mat3(skinMatrix) * tangent.xyz;
    vec4 worldPosition = model * localPosition;
    gl_Position = projection * view * worldPosition;

    vertexTexCoord = texCoord + morphUvOffset.xy;
    fragmentPosition = worldPosition.xyz;
    mat3 modelMatrix = mat3(model);
    mat3 normalMatrix = transpose(inverse(modelMatrix));
    vec3 worldNormal = normalize(normalMatrix * localNormal);
    vec3 worldTangent = modelMatrix * localTangent;
    worldTangent -= worldNormal * dot(worldNormal, worldTangent);
    if (dot(worldTangent, worldTangent) <= 0.000001)
    {
        vec3 reference = abs(worldNormal.y) < 0.999
            ? vec3(0.0, 1.0, 0.0)
            : vec3(1.0, 0.0, 0.0);
        worldTangent = cross(reference, worldNormal);
    }

    float modelHandedness = determinant(modelMatrix) < 0.0 ? -1.0 : 1.0;
    fragmentNormal = worldNormal;
    fragmentTangent = vec4(
        normalize(worldTangent),
        tangent.w * modelHandedness
    );
}
