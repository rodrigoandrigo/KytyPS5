#version 450
layout(location=0) out vec3 color;
void main() { vec2 pos[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(pos[gl_VertexIndex],0,1);color=vec3(0.25,0.5,0.75); }
