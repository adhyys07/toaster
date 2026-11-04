#version 120

varying vec2 v_texcoord0;

void main() {
    gl_Position = gl_Vertex;
    v_texcoord0 = gl_MultiTexCoord0.xy;
}
