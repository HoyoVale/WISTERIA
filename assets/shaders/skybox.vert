#version 330 core

layout(location = 0) in vec3 position;

uniform mat4 view;
uniform mat4 projection;

out vec3 sampleDirection;

void main()
{
    sampleDirection = position;
    vec4 clipPosition = projection * view * vec4(position, 1.0);
    gl_Position = clipPosition.xyww;
}
