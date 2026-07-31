#include "pch.hpp"
#include "Models/cube.hpp"

Cube::Cube(const Transform& transform)
    : Model(cubeData, transform)
{
}
