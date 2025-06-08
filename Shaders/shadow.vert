#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inTexC;
layout(location=3) in vec3 inTangentU;
layout(location=0) out vec2 outTexC;

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
	mat4 shadowTransform;
	vec3 gEyePosW;
	float cbPerObjPad1;
	vec2 RenderTargetSize;
	vec2 InvRenderTargetSize;
	float NearZ;
	float FarZ;
	float TotalTime;
	float DeltaTime;
	vec4 gAmbientLight;
	// Allow application to change fog parameters once per frame.
	// For example, we may only use fog for certain times of day.
	vec4 gFogColor;
	float gFogStart;
	float gFogRange;
	vec2 cbPerObjectPad2;
	
	Light gLights[MAX_LIGHTS];
};


layout (set=1, binding=0) uniform ObjectCB{
	mat4 world;	
	mat4 gTexTransform;
	uint gMaterialIndex;
	uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
};

struct MaterialData
{
    vec4   DiffuseAlbedo;    // 16 bytes
    vec3   FresnelR0;        // 12 bytes
    float  Roughness;        // 4 bytes (now FresnelR0 + Roughness = 16 bytes)
    mat4   MatTransform;     // 64 bytes
    uint   DiffuseMapIndex;  // 4 bytes
    uint   NormalMapIndex;   // 4 bytes
    float  Metal;            // 4 bytes
    float  pad;              // 4 bytes (padding for 16-byte alignment)
};


layout (set=2, binding=0) readonly buffer MaterialBuffer{
	MaterialData materials[];
}materialData;

void main(){
	MaterialData matData = materialData.materials[gMaterialIndex];
	//vec4 posH = vec4(aPos,1.0) * world;
	//gl_Position = posH * viewProj;
	mat4 mvp = viewProj * world;
	gl_Position = mvp * vec4(inPos,1.0);
	
    vec4 texC = gTexTransform  *vec4(inTexC, 0.0f, 1.0f);
	
    outTexC = (matData.MatTransform*texC).xy;
	
}