#version 330 core

uniform vec4 shadowColor;

out vec4 outputColor;

void main()
{
    outputColor = shadowColor;
}
