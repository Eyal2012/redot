#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 surfaceSize;
    float cornerRadius;
    float tint;
    float surfaceOpacity;
    float highlight;
    float hover;
    float grain;
    float outset;
};
void main(){
    vec2 size=max(surfaceSize,vec2(1));float r=min(cornerRadius,min(size.x,size.y)*.5);
    vec2 p=(qt_TexCoord0-.5)*(size+2*outset);vec2 q=abs(p)-(size*.5-r);
    float d=length(max(q,0))+min(max(q.x,q.y),0)-r;
    float mask=1-smoothstep(-.7,.6,d);
    vec2 n=length(max(q,0))>.001?normalize(max(q,0))*sign(p):(q.x>q.y?vec2(sign(p.x),0):vec2(0,sign(p.y)));
    float rim=exp(-pow((d+.45)/.38,2));
    float light=.20+.80*pow(max(0,dot(n,normalize(vec2(-.35,-1)))),3);
    float a=surfaceOpacity+hover*.012;
    vec3 ink=mix(vec3(.17,.20,.24),vec3(.28,.26,.36),tint);
    // Analytical edge reflection only. This shader has no desktop texture.
    float h=rim*(.025+highlight*.16)*light;
    float reflection=exp(-pow((p.y+size.y*.40)/(max(size.y*.12,1)),2))*.006*highlight;
    float noise=fract(sin(dot(floor((p+size*.5)*2),vec2(12.9898,78.233)))*43758.5453)-.5;
    vec3 c=ink*a+vec3(.85,.86,.94)*(h+reflection)+vec3(noise*grain*.018*a);
    a=clamp(a+h+reflection,0,1);
    vec2 sq=abs(p-vec2(0,1.5))-(size*.5-r);
    float sd=length(max(sq,0))+min(max(sq.x,sq.y),0)-r;
    float shadow=.045*exp(-pow(max(sd,0)/2.8,2))*(1-mask);
    fragColor=vec4(c*mask,a*mask+shadow)*qt_Opacity;
}
