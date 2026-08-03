#version 330 core

in vec2 textureCoordinate;

uniform sampler2D accumulationTexture;
uniform sampler2D revealageTexture;

out vec4 outputColor;

void main()
{
    vec4 accumulation = texture(accumulationTexture, textureCoordinate);
    float revealage = clamp(
        texture(revealageTexture, textureCoordinate).r,
        0.0,
        1.0
    );
    float alpha = 1.0 - revealage;
    if (alpha <= 0.0001)
        discard;

    if (isinf(max(max(abs(accumulation.r), abs(accumulation.g)),
            abs(accumulation.b))))
    {
        accumulation.rgb = vec3(accumulation.a);
    }

    vec3 averageColor = accumulation.rgb /
        max(accumulation.a, 0.00001);
    outputColor = vec4(averageColor, alpha);
}
