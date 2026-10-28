#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 world_normal;

layout(push_constant) uniform ModelTransform {
    mat4 model_view_projection;
    mat4 model;
} transform;

void main()
{
    gl_Position =
        transform.model_view_projection * vec4(in_position, 1.0);
    world_normal = mat3(transform.model) * in_normal;
}
