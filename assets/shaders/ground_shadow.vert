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
    // The projected shadow lies exactly on the ground plane and is depth
    // tested with GL_LEQUAL against the ground drawn just before it, so no
    // coplanar bias is needed here. A fixed NDC bias would be distance-
    // dependent and overpaint the character's lower body; instead the
    // character is drawn after this pass and correctly occludes the shadow.
}
