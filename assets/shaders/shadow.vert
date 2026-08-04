#version 330 core

// Depth-only shadow pass. Mirrors mmd.vert's skinning and morph offsets so
// the shadow geometry exactly matches the main pass, but writes only depth.

layout(location = 0) in vec3 position;
layout(location = 7) in vec4 boneIndices;
layout(location = 8) in vec4 boneWeights;
layout(location = 9) in vec3 morphPositionOffset;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int skinningEnabled;
uniform samplerBuffer boneMatrixPalette;

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
    vec4 localPosition =
        skinMatrix * vec4(position + morphPositionOffset, 1.0);
    gl_Position = projection * view * model * localPosition;
}
