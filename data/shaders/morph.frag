#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inLightVec;
layout (location = 2) in vec3 inViewVec;

layout (location = 0) out vec4 outFragColor;

void main()
{
	vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

	vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = reflect(-L, N);
    vec3 diffuse = max(dot(N, L), 0.25) * vec3(1.0);
    float specular = pow(max(dot(R, V), 0.1), 16.0) * color.a;

    outFragColor = vec4(diffuse * color.rgb + specular, 1.0);
}