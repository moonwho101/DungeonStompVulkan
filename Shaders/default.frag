#version 450

layout(location=0) in vec3 inPosW;
layout(location=1) in vec4 inShadowPosH;
layout(location=2) in vec3 inNormalW;
layout(location=3) in vec3 inTangentW;
layout(location=4) in vec2 inTexC;
layout(location=0) out vec4 outFragColor;

struct Light
{
    vec3 Strength;
    float FalloffStart;
    vec3 Direction;
    float FalloffEnd;
    vec3 Position;
    float SpotPower;
};

struct Material
{
    vec4 DiffuseAlbedo;
    vec3 FresnelR0;
    float Roughness;
    float Metal;
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
    uint gTextureIndex;
    uint gTextureNormalIndex;
    uint gObjPad2;
};

struct MaterialData
{
    vec4   DiffuseAlbedo;
    vec3   FresnelR0;
    float  Roughness;
    mat4   MatTransform;
    uint   DiffuseMapIndex;
    uint   NormalMapIndex;
    float  Metal;
    float  pad;
};

layout (set=2, binding=0) readonly buffer MaterialBuffer{
    MaterialData materials[];
}materialData;

layout (set=3,binding=0) uniform sampler samp;
layout (set=3,binding=1) uniform texture2D textureMap[6];
layout (set=4,binding=0) uniform textureCube cubeMap;
layout (set=5,binding=0) uniform sampler2D shadowMap;

layout (constant_id=0) const int NUM_DIR_LIGHTS=0;
layout (constant_id=1) const int enableNormalMap=0;
layout (constant_id=2) const int enableAlphaTest=0;
layout (constant_id=3) const int NUM_POINT_LIGHTS=16;
layout (constant_id=4) const int NUM_SPOT_LIGHTS=10;

// Helper functions
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return clamp((falloffEnd-d)/(falloffEnd-falloffStart),0,1);
}

vec3 NormalSampleToWorldSpace(vec3 normalMapSample, vec3 unitNormalW, vec3 tangentW)
{
    vec3 normalT = 2.0f*normalMapSample - vec3(1.0f);
    vec3 N = unitNormalW;
    vec3 T = normalize(tangentW - dot(tangentW, N)*N);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    return TBN * normalT;
}

float CalcShadowFactor(vec4 shadowPosH)
{
    shadowPosH.xyz /= shadowPosH.w;
    float depth = shadowPosH.z;
    ivec2 texSize = textureSize(shadowMap,0);
    float dx = 1.0f / float(texSize.x);
    float percentLit = 0.0f;
    const vec2 offsets[9] = {
        vec2(-dx,  -dx), vec2(0.0f,  -dx), vec2(dx,  -dx),
        vec2(-dx, 0.0f), vec2(0.0f, 0.0f), vec2(dx, 0.0f),
        vec2(-dx,  +dx), vec2(0.0f,  +dx), vec2(dx,  +dx)
    };
    for(int i = 0; i < 9; ++i)
    {
        float shadow=1.0;
        float dist = texture(shadowMap,vec2(shadowPosH.st+offsets[i])).r;
        if(dist < depth)
            shadow = dist;
        percentLit += shadow;
    }
    return percentLit / 9.0f;
}

// PBR Functions
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 ComputePBRLight(Light L, Material mat, vec3 pos, vec3 N, vec3 V, vec3 shadowFactor)
{
    vec3 Lo = vec3(0.0);
    vec3 F0 = mix(vec3(0.04), mat.FresnelR0, mat.Metal);
    vec3 Lvec;
    float attenuation = 1.0;
    float spot = 1.0;

    // Directional
    if (L.FalloffEnd == 0.0 && L.SpotPower == 0.0) {
        Lvec = -L.Direction;
    }
    // Point
    else if (L.SpotPower == 0.0) {
        Lvec = L.Position - pos;
        float d = length(Lvec);
        if(d > L.FalloffEnd) return vec3(0.0);
        Lvec /= d;
        attenuation = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    }
    // Spot
    else {
        Lvec = L.Position - pos;
        float d = length(Lvec);
        if(d > L.FalloffEnd) return vec3(0.0);
        Lvec /= d;
        attenuation = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
        spot = pow(max(dot(-Lvec, L.Direction), 0.0f), L.SpotPower);
    }

    vec3 H = normalize(V + Lvec);
    float NDF = DistributionGGX(N, H, mat.Roughness);
    float G = GeometrySmith(N, V, Lvec, mat.Roughness);
    float NdotL = max(dot(N, Lvec), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - mat.Metal;

    vec3 radiance = L.Strength * attenuation * spot;

    Lo = (kD * mat.DiffuseAlbedo.rgb / PI + specular) * radiance * NdotL * shadowFactor.x;
    return Lo;
}

void main(){
    MaterialData matData = materialData.materials[gMaterialIndex];
    vec4 diffuseAlbedo = matData.DiffuseAlbedo;
    vec3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    float metal = matData.Metal;

    uint diffuseTexIndex = gTextureIndex;
    uint normalMapIndex = gTextureNormalIndex;

    diffuseAlbedo *= texture(sampler2D(textureMap[diffuseTexIndex],samp),inTexC);

    if(diffuseAlbedo.a -0.1 < 0.0)
        discard;

    if(enableAlphaTest>0){
        outFragColor = texture(sampler2D(textureMap[diffuseTexIndex],samp),inTexC);
        return;
    }

    vec3 norm = normalize(inNormalW);
    vec3 bumpedNormalW = norm;
    vec4 normalMapSample = vec4(1.0);

    if(enableNormalMap>0){
        normalMapSample = texture(sampler2D(textureMap[normalMapIndex],samp), inTexC);    
        bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, norm, inTangentW);
    }

    vec3 toEyeW = normalize(gEyePosW - inPosW);    
    vec3 camFrag =  inPosW - gEyePosW;
    float distToEye = length(camFrag);

    vec4 ambient = gAmbientLight * diffuseAlbedo;

    vec3 shadowFactor = vec3(1.0f, 1.0f, 1.0f);
    shadowFactor[0] = CalcShadowFactor(inShadowPosH);

    Material mat = { diffuseAlbedo, fresnelR0, roughness, metal };

    vec3 N = normalize(bumpedNormalW);
    vec3 V = normalize(toEyeW);
    vec3 color = vec3(0.0);

    // Directional lights
    for(int i=0; i<NUM_DIR_LIGHTS; ++i)
        color += ComputePBRLight(gLights[i], mat, inPosW, N, V, shadowFactor);

    // Point lights
    for(int i=NUM_DIR_LIGHTS; i<NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
        color += ComputePBRLight(gLights[i], mat, inPosW, N, V, shadowFactor);

    // Spot lights
    for(int i=NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; i<NUM_DIR_LIGHTS+NUM_POINT_LIGHTS+NUM_SPOT_LIGHTS; ++i)
        color += ComputePBRLight(gLights[i], mat, inPosW, N, V, shadowFactor);

    // IBL (simple reflection, not full PBR IBL)
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), fresnelR0, metal);
    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 envColor = texture(samplerCube(cubeMap,samp), R).rgb;
    color += F * envColor * 0.2; // 0.2 = simple IBL strength

    // Add ambient
    color += ambient.rgb * (1.0 - metal);

    // Fog
    float fogAmount = clamp((distToEye - gFogStart) / gFogRange, 0.0, 1.0);
    vec3 finalColor = mix(color, gFogColor.rgb, fogAmount);

    outFragColor = vec4(finalColor, diffuseAlbedo.a);
}
