#version 330 core

in vec3 sampleDirection;
out vec4 outputColor;

uniform samplerCube environmentMap;
uniform float environmentIntensity;

void main()
{
    vec3 color = texture(environmentMap, normalize(sampleDirection)).rgb *
        environmentIntensity;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outputColor = vec4(color, 1.0);
}
