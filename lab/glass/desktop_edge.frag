#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {mat4 qt_Matrix;float qt_Opacity;vec2 surfaceSize;vec2 sourceSize;vec2 origin;float cornerRadius;float edgeStrength;float tint;float fillStrength;float highlight;};
layout(binding=1) uniform sampler2D scene;
void main(){
    vec2 p=(qt_TexCoord0-.5)*surfaceSize;float r=min(cornerRadius,min(surfaceSize.x,surfaceSize.y)*.5);vec2 q=abs(p)-(surfaceSize*.5-r);
    float d=length(max(q,0))+min(max(q.x,q.y),0)-r;
    vec2 n=length(max(q,0))>.001?normalize(max(q,0))*sign(p):(q.x>q.y?vec2(sign(p.x),0):vec2(0,sign(p.y)));
    // Local curved-edge lens, vanishes completely past a 30-physical-pixel band.
    float band=pow(clamp(1.+d/30.,0.,1.),2.);float bend=edgeStrength*band;
    vec2 uv=clamp((origin+qt_TexCoord0*surfaceSize-n*bend)/sourceSize,vec2(.0001),vec2(.9999));
    vec3 c=texture(scene,uv).rgb;
    c=mix(c,mix(vec3(.055,.065,.08),vec3(.13,.10,.17),tint),fillStrength);
    float rim=exp(-pow((d+.6)/.45,2.));float light=.2+.8*pow(max(0.,dot(n,normalize(vec2(-.35,-1.)))),3.);
    c+=vec3(.85,.86,.94)*rim*highlight*.16*light;
    float mask=1.-smoothstep(-.7,.5,d);fragColor=vec4(c,1)*mask*qt_Opacity;
}
