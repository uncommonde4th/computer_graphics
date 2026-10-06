#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    vec4 position;
    vec4 rotation;
    vec4 scale;
    vec4 tint;
    vec4 camera; // x: 1 - перспектива, 0 - ортографическая; y: fov (рад) или полувысота; z: расстояние; w: aspect
} pc;

layout(location = 0) out vec3 fragColor;

mat3 rotX(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0,  0, c, s,  0, -s, c); }
mat3 rotY(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s,  0, 1, 0,  s, 0, c); }
mat3 rotZ(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0,  -s, c, 0,  0, 0, 1); }

void main() {
    // Модель: растяжение поворот перенос
    mat3 r = rotZ(pc.rotation.z) * rotY(pc.rotation.y) * rotX(pc.rotation.x);
    vec3 world = r * (inPosition * pc.scale.xyz) + pc.position.xyz;

    // Камера смотрит вдоль -Z с расстояния distance
    vec3 v = world - vec3(0.0, 0.0, pc.camera.z);

    const float n = 0.1;
    const float f = 100.0;
    float aspect = pc.camera.w;

    vec4 clip;
    if (pc.camera.x > 0.5) {
        // Перспектива (Vulkan: глубина 0..1, Y вниз)
        float t = 1.0 / tan(pc.camera.y * 0.5);
        clip = vec4(t / aspect * v.x, -t * v.y, f / (n - f) * v.z + n * f / (n - f), -v.z);
    } else {
        // Ортографическая
        float h = pc.camera.y;
        clip = vec4(v.x / (h * aspect), -v.y / h, v.z / (n - f) + n / (n - f), 1.0);
    }

    gl_Position = clip;
    fragColor = inColor * pc.tint.rgb;
}
