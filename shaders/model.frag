#version 450

layout(location = 0) in vec3 world_normal;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 normal = normalize(world_normal);
    vec3 light_direction = normalize(vec3(-0.35, 0.55, 1.0));
    float diffuse = max(dot(normal, light_direction), 0.0);
    vec3 surface_color = vec3(0.72, 0.58, 0.43);
    vec3 lit_color = surface_color * (0.22 + (0.78 * diffuse));
    out_color = vec4(lit_color, 1.0);
}
