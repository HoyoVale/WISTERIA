#version 330 core

// MMD ground shadow: the CPU uploads a planar projection matrix
// (world -> ground plane along the light direction) folded into "view", so
// this pass renders the model's already-skinned silhouette flattened onto
// the ground. Only the current vertex positions are needed; no skinning here.

layout(location = 0) in vec3 position;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(position, 1.0);
    // Coplanar decal bias: the projected shadow lies exactly on the ground
    // plane, so its depth can be rejected by the ground's own depth. Pull it
    // slightly toward the camera; the character still occludes it correctly.
    gl_Position.z -= 0.002 * gl_Position.w;
}
