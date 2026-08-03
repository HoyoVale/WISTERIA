#version 330 core

in vec3 localPosition;
out vec4 outputColor;

uniform sampler2D equirectangularMap;

const vec2 inverseAtan = vec2(0.15915494309, 0.31830988618);

void main()
{
    vec3 direction = normalize(localPosition);
    vec2 uv = vec2(
        atan(direction.z, direction.x),
        asin(clamp(direction.y, -1.0, 1.0))
    );
    uv = uv * inverseAtan + 0.5;
    outputColor = vec4(texture(equirectangularMap, uv).rgb, 1.0);
}
