#version 330 core

in vec2 textureCoordinate;

uniform sampler2D sceneColorTexture;
uniform bool fxaaEnabled;
uniform vec2 inverseScreenSize;
uniform float minimumContrast;
uniform float relativeContrast;
uniform float subpixelBlending;

out vec4 outputColor;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

float SampleLuminance(vec2 coordinate)
{
    return Luminance(texture(sceneColorTexture, coordinate).rgb);
}

void main()
{
    vec4 centerColor = texture(sceneColorTexture, textureCoordinate);
    if (!fxaaEnabled)
    {
        outputColor = centerColor;
        return;
    }

    vec2 northOffset = vec2(0.0, inverseScreenSize.y);
    vec2 eastOffset = vec2(inverseScreenSize.x, 0.0);

    float center = Luminance(centerColor.rgb);
    float north = SampleLuminance(textureCoordinate + northOffset);
    float east = SampleLuminance(textureCoordinate + eastOffset);
    float south = SampleLuminance(textureCoordinate - northOffset);
    float west = SampleLuminance(textureCoordinate - eastOffset);

    float highest = max(center, max(max(north, east), max(south, west)));
    float lowest = min(center, min(min(north, east), min(south, west)));
    float contrast = highest - lowest;
    float threshold = max(minimumContrast, highest * relativeContrast);
    if (contrast < threshold)
    {
        outputColor = centerColor;
        return;
    }

    float northEast = SampleLuminance(
        textureCoordinate + northOffset + eastOffset
    );
    float southEast = SampleLuminance(
        textureCoordinate - northOffset + eastOffset
    );
    float southWest = SampleLuminance(
        textureCoordinate - northOffset - eastOffset
    );
    float northWest = SampleLuminance(
        textureCoordinate + northOffset - eastOffset
    );

    float horizontalStrength =
        2.0 * abs(north + south - 2.0 * center) +
        abs(northEast + southEast - 2.0 * east) +
        abs(northWest + southWest - 2.0 * west);
    float verticalStrength =
        2.0 * abs(east + west - 2.0 * center) +
        abs(northEast + northWest - 2.0 * north) +
        abs(southEast + southWest - 2.0 * south);
    bool horizontalEdge = horizontalStrength >= verticalStrength;

    float firstLuminance = horizontalEdge ? north : west;
    float secondLuminance = horizontalEdge ? south : east;
    float firstGradient = abs(firstLuminance - center);
    float secondGradient = abs(secondLuminance - center);

    vec2 perpendicularStep = horizontalEdge
        ? vec2(0.0, inverseScreenSize.y)
        : vec2(inverseScreenSize.x, 0.0);
    float oppositeLuminance = firstLuminance;
    float largestGradient = firstGradient;
    if (secondGradient > firstGradient)
    {
        perpendicularStep = -perpendicularStep;
        oppositeLuminance = secondLuminance;
        largestGradient = secondGradient;
    }

    float edgeLuminance = 0.5 * (center + oppositeLuminance);
    float gradientThreshold = largestGradient * 0.25;
    vec2 edgeStep = horizontalEdge
        ? vec2(inverseScreenSize.x, 0.0)
        : vec2(0.0, inverseScreenSize.y);
    vec2 edgeCoordinate = textureCoordinate + perpendicularStep * 0.5;
    vec2 firstCoordinate = edgeCoordinate - edgeStep;
    vec2 secondCoordinate = edgeCoordinate + edgeStep;

    float firstDelta = SampleLuminance(firstCoordinate) - edgeLuminance;
    float secondDelta = SampleLuminance(secondCoordinate) - edgeLuminance;
    bool firstReached = abs(firstDelta) >= gradientThreshold;
    bool secondReached = abs(secondDelta) >= gradientThreshold;

    for (int stepIndex = 0; stepIndex < 12; ++stepIndex)
    {
        if (!firstReached)
        {
            firstCoordinate -= edgeStep;
            firstDelta = SampleLuminance(firstCoordinate) - edgeLuminance;
            firstReached = abs(firstDelta) >= gradientThreshold;
        }
        if (!secondReached)
        {
            secondCoordinate += edgeStep;
            secondDelta = SampleLuminance(secondCoordinate) - edgeLuminance;
            secondReached = abs(secondDelta) >= gradientThreshold;
        }
        if (firstReached && secondReached)
            break;
    }

    float firstDistance = horizontalEdge
        ? textureCoordinate.x - firstCoordinate.x
        : textureCoordinate.y - firstCoordinate.y;
    float secondDistance = horizontalEdge
        ? secondCoordinate.x - textureCoordinate.x
        : secondCoordinate.y - textureCoordinate.y;
    bool firstIsNearest = firstDistance < secondDistance;
    float nearestDistance = min(firstDistance, secondDistance);
    float edgeLength = max(firstDistance + secondDistance, 0.000001);
    float nearestDelta = firstIsNearest ? firstDelta : secondDelta;
    bool centerIsDarker = center < edgeLuminance;
    bool nearestIsDarker = nearestDelta < 0.0;
    float edgeBlend = centerIsDarker == nearestIsDarker
        ? 0.0
        : 0.5 - nearestDistance / edgeLength;

    float neighborhoodAverage = (
        2.0 * (north + east + south + west) +
        northEast + southEast + southWest + northWest
    ) / 12.0;
    float subpixelBlend = clamp(
        abs(neighborhoodAverage - center) / max(contrast, 0.000001),
        0.0,
        1.0
    );
    subpixelBlend = smoothstep(0.0, 1.0, subpixelBlend);
    subpixelBlend = subpixelBlend * subpixelBlend * subpixelBlending;

    float finalBlend = max(edgeBlend, subpixelBlend);
    vec2 finalCoordinate =
        textureCoordinate + perpendicularStep * finalBlend;
    vec4 filteredColor = texture(sceneColorTexture, finalCoordinate);
    outputColor = vec4(filteredColor.rgb, centerColor.a);
}
