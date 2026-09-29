#version 450
void main(){vec2 p[6]=vec2[](vec2(-.5,-1),vec2(.5,-1),vec2(-.5,1),vec2(-.5,1),vec2(.5,-1),vec2(.5,1));gl_Position=vec4(p[gl_VertexIndex],0,1);}
