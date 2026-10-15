#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
 mat4 qt_Matrix; float qt_Opacity; vec2 surfaceSize; float cornerRadius;
 float solid; vec4 tintColor; vec4 solidColor; vec4 edgeColor;
 float fillStrength; float edgeStrength;
};
void main(){
 vec2 size=max(surfaceSize,vec2(1));
 float r=min(cornerRadius,min(size.x,size.y)*.5);
 vec2 q=abs((qt_TexCoord0-.5)*size)-(size*.5-r);
 float d=length(max(q,0))+min(max(q.x,q.y),0)-r;
 float coverage=1-smoothstep(-.7,.6,d);
 float rim=exp(-pow((d+.65)/.55,2.))*edgeStrength;
 float a=mix(fillStrength,1.,solid);
 vec3 c=mix(tintColor.rgb,solidColor.rgb,solid)*a;
 // QColor uniforms arrive premultiplied. rim already supplies the optical
 // edge alpha; recover straight RGB before premultiplying with that alpha.
 c+=(edgeColor.a>0.?edgeColor.rgb/edgeColor.a:vec3(0.))*rim; a=clamp(a+rim,0.,1.);
 fragColor=vec4(c,a)*coverage*qt_Opacity;
}
