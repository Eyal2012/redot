#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {mat4 qt_Matrix;float qt_Opacity;vec2 textureSize;vec2 axis;float blurRadius;};
layout(binding=1) uniform sampler2D scene;
void main(){
    if(blurRadius<.01){fragColor=texture(scene,qt_TexCoord0)*qt_Opacity;return;}
    vec4 c=vec4(0);float total=0;float sigma=max(.25,blurRadius/2.5);
    for(int i=-8;i<=8;++i){float delta=float(i)*blurRadius/8.;float w=exp(-.5*delta*delta/(sigma*sigma));c+=texture(scene,clamp(qt_TexCoord0+axis*delta/textureSize,vec2(0),vec2(1)))*w;total+=w;}
    fragColor=c/total*qt_Opacity;
}
