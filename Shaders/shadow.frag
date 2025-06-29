#version 450

layout(location=0) in vec2 inTexC;
layout(location=0) out vec4 outFragColor;

struct Light
{
    vec3 Strength;
    float FalloffStart; // point/spot light only
    vec3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    vec3 Position;    // point light only
    float SpotPower;    // spot light only
};

struct Material
{
    vec4 DiffuseAlbedo;
    vec3 FresnelR0;
    float Shininess;
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
	mat4 shadowTransform;
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


layout (set=3,binding=0) uniform sampler samp;
layout (set=3,binding=1) uniform texture2D textureMap[6];

layout (set=4,binding=0) uniform textureCube cubeMap;


layout (constant_id=0) const int NUM_DIR_LIGHTS=3;
layout (constant_id=1) const int enableNormalMap=0;
layout (constant_id=2) const int enableAlphaTest=0;


void main(){
	//MaterialData matData = gMaterialData[gMaterialIndex];
	MaterialData matData = materialData.materials[gMaterialIndex];
	//float4 diffuseAlbedo = matData.DiffuseAlbedo;
	vec4 diffuseAlbedo = matData.DiffuseAlbedo;
	//float3 fresnelR0 = matData.FresnelR0;	
	vec3 fresnelR0 = matData.FresnelR0;
	float roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;
	
	// Dynamically look up the texture in the array.
	//diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	diffuseAlbedo *= texture(sampler2D(textureMap[diffuseTexIndex],samp),inTexC);
	
	if(enableAlphaTest>0){
		if(diffuseAlbedo.a-0.1f < 0)
			discard;		
	}
	
	
	outFragColor = diffuseAlbedo;
	
}