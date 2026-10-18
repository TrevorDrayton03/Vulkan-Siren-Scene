#version 450

layout(location = 0) in vec3 normal;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(abs(normalize(normal)), 1.0);
}
