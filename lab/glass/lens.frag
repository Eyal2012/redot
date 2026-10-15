#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 surfaceSize;
    vec2 sceneSize;
    vec2 origin;
    float cornerRadius;
    float refraction;
    float localBlur;
    float tint;
    float surfaceOpacity;
    float highlight;
    float grain;
};
layout(binding=1) uniform sampler2D scene;
void main(){
    vec2 p=(qt_TexCoord0-.5)*surfaceSize;float r=min(cornerRadius,min(surfaceSize.x,surfaceSize.y)*.5);
    vec2 q=abs(p)-(surfaceSize*.5-r);float d=length(max(q,0))+min(max(q.x,q.y),0)-r;
    vec2 n=length(max(q,0))>.001?normalize(max(q,0))*sign(p):(q.x>q.y?vec2(sign(p.x),0):vec2(0,sign(p.y)));
    float bend=exp(min(d,0)/14.0)*refraction;
    vec2 uv=clamp((origin+qt_TexCoord0*surfaceSize-n*bend)/sceneSize,vec2(.001),vec2(.999));
    vec2 b=vec2(localBlur)/sceneSize;
    vec3 sampleColor=texture(scene,uv).rgb*.4;
    sampleColor+=(texture(scene,clamp(uv+vec2(b.x,0),vec2(.001),vec2(.999))).rgb+texture(scene,clamp(uv-vec2(b.x,0),vec2(.001),vec2(.999))).rgb+texture(scene,clamp(uv+vec2(0,b.y),vec2(.001),vec2(.999))).rgb+texture(scene,clamp(uv-vec2(0,b.y),vec2(.001),vec2(.999))).rgb)*.15;
    vec3 ink=mix(vec3(.14,.18,.22),vec3(.25,.23,.33),tint);
    vec3 c=mix(sampleColor,ink,surfaceOpacity);
    float rim=exp(-pow((d+.45)/.38,2));float light=.2+.8*pow(max(0,dot(n,normalize(vec2(-.35,-1)))),3);
    c+=vec3(.85,.86,.94)*rim*(.025+highlight*.16)*light;
    float noise=fract(sin(dot(floor(qt_TexCoord0*surfaceSize*2),vec2(12.9898,78.233)))*43758.5453)-.5;
    c+=vec3(noise*grain*.018);
    float mask=1-smoothstep(-.7,.6,d);
    fragColor=vec4(c,1)*mask*qt_Opacity;
}
