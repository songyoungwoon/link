#pragma once
namespace linkworld::model_shaders {
inline constexpr const char* vertex=R"GLSL(#version 120
varying vec3 eyePosition,eyeNormal,surfacePosition,surfaceNormal;
varying vec2 texcoord;
varying vec4 vertexColor,lightPosition;
uniform float outlineWidth,shadowNormalBias;
uniform mat4 modelWorld,modelNormal,lightViewProjection;
void main(){
 vec4 local=gl_Vertex;local.xyz+=gl_Normal*outlineWidth;
 vec4 eye=gl_ModelViewMatrix*local;
 eyePosition=eye.xyz;eyeNormal=normalize(gl_NormalMatrix*gl_Normal);
 surfacePosition=(modelWorld*local).xyz;
 surfaceNormal=normalize((modelNormal*vec4(gl_Normal,0.0)).xyz);
 lightPosition=lightViewProjection*vec4(surfacePosition+surfaceNormal*shadowNormalBias,1.0);
 texcoord=gl_MultiTexCoord0.xy;vertexColor=gl_Color;
 gl_Position=gl_ProjectionMatrix*eye;
}
)GLSL";
// The caller prepends version/extension directives. Legacy worlds do not require
// texture LOD or FBO support; lighting-v1 explicitly enables the advanced branch.
inline constexpr const char* fragment=R"GLSL(
varying vec3 eyePosition,eyeNormal,surfacePosition,surfaceNormal;
varying vec2 texcoord;
varying vec4 vertexColor,lightPosition;
uniform sampler2D colorMap,normalMap,metallicRoughnessMap,occlusionMap,emissionMap;
uniform int hasColorMap,hasNormalMap,hasMetallicRoughnessMap,hasOcclusionMap,hasEmissionMap;
uniform vec4 baseColor;
uniform vec3 tint,emissive,eyeUp,cameraWorld;
uniform float opacity,roughness,metalness,styleEmission,normalScale,occlusionStrength;
uniform int alphaMode,doubleSided,unlit,toon,outlinePass,fogEnabled;
uniform float alphaCutoff;
#ifdef LINK_ADVANCED_LIGHTING
uniform sampler2DShadow shadowMap;
uniform sampler2D irradianceMap,specularMap;
uniform int hasEnvironment,hasShadows,pointCount;
uniform float shadowBias,shadowTexel,exposure,sunIntensity,ambientIntensity,environmentIntensity;
uniform vec3 sunDirection,sunColor,skyColor,groundColor;
uniform vec4 pointPositionRange[4];
uniform vec3 pointColorIntensity[4];
#endif
const float PI=3.141592653589793;
vec3 fresnel(float cosine,vec3 f0){return f0+(1.0-f0)*pow(1.0-cosine,5.0);}
float visibility(float cosine,float k){return cosine/(cosine*(1.0-k)+k);}
vec3 directLight(vec3 n,vec3 v,vec3 l,vec3 radiance,vec3 albedo,vec3 f0,float r,float m){
 vec3 sum=v+l;vec3 h=length(sum)>0.00001?normalize(sum):n;
 float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.001),nh=max(dot(n,h),0.0);
 float a=r*r,a2=a*a,d=nh*nh*(a2-1.0)+1.0;
 float distribution=a2/max(PI*d*d,0.00001),k=(r+1.0)*(r+1.0)/8.0;
 vec3 f=fresnel(max(dot(h,v),0.0),f0);
 vec3 specular=distribution*visibility(nv,k)*visibility(nl,k)*f/max(4.0*nv*nl,0.0001);
 return ((1.0-f)*(1.0-m)*albedo/PI+specular)*radiance*nl;
}
vec3 mappedNormal(vec3 n,vec3 position){
 if(hasNormalMap==0)return n;
 vec3 q1=dFdx(position),q2=dFdy(position);vec2 st1=dFdx(texcoord),st2=dFdy(texcoord);
 vec3 dp2perp=cross(q2,n),dp1perp=cross(n,q1);
 vec3 t=dp2perp*st1.x+dp1perp*st2.x,b=dp2perp*st1.y+dp1perp*st2.y;
 float denominator=max(dot(t,t),dot(b,b));
 if(denominator<0.0000000001)return n;
 float scale=inversesqrt(denominator);vec3 sampled=texture2D(normalMap,texcoord).xyz*2.0-1.0;
 sampled.xy*=normalScale;
 return normalize(t*scale*sampled.x+b*scale*sampled.y+n*sampled.z);
}
#ifdef LINK_ADVANCED_LIGHTING
vec2 envUv(vec3 d){d=normalize(d);return vec2(atan(d.z,d.x)/(2.0*PI)+0.5,acos(clamp(d.y,-1.0,1.0))/PI);}
vec2 envBRDF(float r,float nv){
 vec4 v=r*vec4(-1.0,-0.0275,-0.572,0.022)+vec4(1.0,0.0425,1.04,-0.04);
 float a=min(v.x*v.x,exp2(-9.28*nv))*v.x+v.y;
 return vec2(-1.04,1.04)*a+v.zw;
}
float shadowVisibility(vec3 n){
 if(hasShadows==0)return 1.0;
 vec3 p=lightPosition.xyz/lightPosition.w;p=p*0.5+0.5;
 if(p.x<=0.0||p.x>=1.0||p.y<=0.0||p.y>=1.0||p.z<=0.0||p.z>=1.0)return 1.0;
 float bias=shadowBias*(1.0+2.0*(1.0-max(dot(n,sunDirection),0.0))),sum=0.0;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)sum+=shadow2D(shadowMap,vec3(p.xy+vec2(float(x),float(y))*shadowTexel,p.z-bias)).r;
 return sum/9.0;
}
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
#endif
void main(){
 vec4 sampled=hasColorMap!=0?texture2D(colorMap,texcoord):vec4(1.0);
 float coverage=sampled.a*baseColor.a*vertexColor.a;
 if(alphaMode==1&&coverage<alphaCutoff)discard;
 float alpha=(alphaMode==2?coverage:1.0)*opacity;if(alpha<0.001)discard;
 vec3 n=normalize(eyeNormal),position=eyePosition;
#ifdef LINK_ADVANCED_LIGHTING
 n=normalize(surfaceNormal);position=surfacePosition;
#endif
 if(doubleSided!=0&&!gl_FrontFacing)n=-n;
 n=mappedNormal(n,position);
 vec3 outputColor;
 if(outlinePass!=0){outputColor=vec3(0.028,0.035,0.070);}
 else{
  vec3 albedo=pow(max(sampled.rgb,vec3(0.0)),vec3(2.2))*baseColor.rgb*vertexColor.rgb*pow(tint,vec3(2.2));
  vec3 e=emissive;if(hasEmissionMap!=0)e*=pow(max(texture2D(emissionMap,texcoord).rgb,vec3(0.0)),vec3(2.2));
  vec3 glow=e+albedo*styleEmission;
  vec4 mr=hasMetallicRoughnessMap!=0?texture2D(metallicRoughnessMap,texcoord):vec4(1.0);
  float r=clamp(roughness*mr.g,0.06,1.0);
  float m=clamp(metalness*mr.b,0.0,1.0);
  float ao=hasOcclusionMap!=0?mix(1.0,texture2D(occlusionMap,texcoord).r,occlusionStrength):1.0;
#ifdef LINK_ADVANCED_LIGHTING
  vec3 v=normalize(cameraWorld-surfacePosition),f0=mix(vec3(0.04),albedo,m);
  if(unlit!=0){outputColor=albedo+glow;}
  else{
   float hemi=clamp(n.y*0.5+0.5,0.0,1.0);
   vec3 ambient=mix(groundColor,skyColor,hemi)*ambientIntensity;
   float nv=max(dot(n,v),0.001);vec3 f=fresnel(nv,f0);
   outputColor=albedo*(1.0-m)*(1.0-f)*ambient*ao;
   if(hasEnvironment!=0){
    vec3 irradiance=texture2DLod(irradianceMap,envUv(n),0.0).rgb;
    vec3 reflected=reflect(-v,n);vec2 brdf=envBRDF(r,nv);
    vec3 specular=texture2DLod(specularMap,envUv(reflected),r*8.0).rgb;
    outputColor+=environmentIntensity*(albedo*(1.0-m)*(1.0-f)*irradiance+specular*(f0*brdf.x+brdf.y))*ao;
   }
   float shadow=shadowVisibility(n);
   if(toon!=0){float key=max(dot(n,sunDirection),0.0);float bands=.25+.35*smoothstep(.2,.24,key)+.4*smoothstep(.62,.66,key);outputColor+=albedo*sunColor*sunIntensity*bands*shadow*.35;}
   else outputColor+=directLight(n,v,sunDirection,sunColor*sunIntensity,albedo,f0,r,m)*shadow;
   for(int i=0;i<4;++i){if(i>=pointCount)break;vec3 delta=pointPositionRange[i].xyz-surfacePosition;float distance=length(delta),range=pointPositionRange[i].w;
    float cutoff=clamp(1.0-pow(distance/range,4.0),0.0,1.0);float attenuation=cutoff*cutoff/max(distance*distance,.04);
    if(distance>0.0001)outputColor+=directLight(n,v,delta/distance,pointColorIntensity[i]*attenuation,albedo,f0,r,m);
   }
   outputColor+=glow;
  }
  outputColor=pow(aces(max(outputColor*exposure,vec3(0.0))),vec3(1.0/2.2));
#else
  if(unlit!=0){outputColor=albedo+glow;}
  else{
   vec3 l0=normalize(gl_LightSource[0].position.xyz-eyePosition*gl_LightSource[0].position.w),l1=normalize(gl_LightSource[1].position.xyz-eyePosition*gl_LightSource[1].position.w),v=normalize(-eyePosition);
   float hemi=clamp(dot(n,normalize(eyeUp))*0.5+0.5,0.0,1.0);vec3 ambient=mix(vec3(.10,.125,.18),vec3(.37,.45,.52),hemi);
   if(toon!=0){float key=max(dot(n,l0),0.0),bands=.38+.28*smoothstep(.17,.21,key)+.30*smoothstep(.62,.66,key),fill=max(dot(n,l1),0.0)*.13;vec3 rim=vec3(.16,.23,.32)*pow(1.0-max(dot(n,v),0.0),3.0)*max(dot(n,l1),0.0);outputColor=albedo*(vec3(bands)+ambient*.23+fill)+rim+glow;}
   else{vec3 f0=mix(vec3(.04),albedo,m);outputColor=(albedo*ambient*(1.0-m)*.55+f0*ambient*.12)*ao+directLight(n,v,l0,gl_LightSource[0].diffuse.rgb*3.0,albedo,f0,r,m)+directLight(n,v,l1,gl_LightSource[1].diffuse.rgb*2.4,albedo,f0,r,m)+glow;}
  }
  float peak=max(outputColor.r,max(outputColor.g,outputColor.b));outputColor/=1.0+max(peak-1.0,0.0)*.55;outputColor=pow(max(outputColor,vec3(0.0)),vec3(1.0/2.2));
#endif
 }
 if(fogEnabled!=0){float fog=clamp((gl_Fog.end-abs(eyePosition.z))/max(gl_Fog.end-gl_Fog.start,.0001),0.0,1.0);outputColor=mix(gl_Fog.color.rgb,outputColor,fog);}
 gl_FragColor=vec4(outputColor,alpha);
}
)GLSL";
inline constexpr const char* depthVertex=R"GLSL(#version 120
varying vec2 uv;varying float vertexAlpha;
void main(){uv=gl_MultiTexCoord0.xy;vertexAlpha=gl_Color.a;gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}
)GLSL";
inline constexpr const char* depthFragment=R"GLSL(#version 120
varying vec2 uv;varying float vertexAlpha;
uniform sampler2D colorMap;uniform int masked,hasMap;uniform float cutoff,baseAlpha;
void main(){if(masked!=0){float a=baseAlpha*vertexAlpha;if(hasMap!=0)a*=texture2D(colorMap,uv).a;if(a<cutoff)discard;}gl_FragColor=vec4(1.0);}
)GLSL";
inline constexpr const char* skyVertex=R"GLSL(#version 120
varying vec2 screen;void main(){screen=gl_Vertex.xy;gl_Position=gl_Vertex;}
)GLSL";
inline constexpr const char* skyFragment=R"GLSL(#version 120
varying vec2 screen;uniform sampler2D radianceMap;uniform mat4 inverseProjection,inverseView;uniform float exposure;
void main(){vec4 eye=inverseProjection*vec4(screen,1.0,1.0);vec3 direction=normalize((inverseView*vec4(eye.xyz/eye.w,0.0)).xyz);
 vec2 uv=vec2(atan(direction.z,direction.x)/(6.28318530718)+.5,acos(clamp(direction.y,-1.0,1.0))/3.14159265359);
 vec3 x=texture2D(radianceMap,uv).rgb*exposure;vec3 tone=clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.0,1.0);gl_FragColor=vec4(pow(tone,vec3(1.0/2.2)),1.0);}
)GLSL";
}
