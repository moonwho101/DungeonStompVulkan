#version 450
layout (location=0) in vec3 inPosL;
layout (location=0) out vec4 outFragCol;

struct Light
{
    vec3 Strength;
    float FalloffStart; // point/spot light only
    vec3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    vec3 Position;    // point light only
    float SpotPower;    // spot light only
};
#define MAX_LIGHTS 31


layout (set=0,binding=0) uniform PassCB{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 invProj;
	mat4 viewProj;
	mat4 invViewProj;
	vec3 gEyePosW;
	float cbPerObjPad1;
	vec2 RenderTargetSize;
	vec2 InvRenderTargetSize;
	float NearZ;
	float FarZ;
	float TotalTime;
	float DeltaTime;
	vec4 gAmbientLight;
	Light gLights[MAX_LIGHTS];
};


layout (set=1, binding=0) uniform ObjectCB{
	mat4 gWorld;	
	mat4 gTexTransform;
	uint gMaterialIndex;
	uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
};

layout (set=3,binding=0) uniform sampler samp;
layout (set=3,binding=1) uniform texture2D textureMap[6];


layout (set=4,binding=0) uniform textureCube cubeMap;

void main(){
	
	 outFragCol = texture(samplerCube(cubeMap,samp),inPosL);
}