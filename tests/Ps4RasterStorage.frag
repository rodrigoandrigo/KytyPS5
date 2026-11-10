#version 450

layout(set = 0, binding = 58, std430) coherent volatile buffer PixelStorage {
    uint values[];
} storage_buffer[1];
layout(location = 0) out vec4 color;

void main() {
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uint index = pixel.y * 64u + pixel.x;
    storage_buffer[0].values[index] = 0xffbf8040u;
    memoryBarrierBuffer();
    color = unpackUnorm4x8(storage_buffer[0].values[index]);
}
