#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;

struct Light
{
    vec3 Strength;
    float FalloffStart;
    vec3 Direction;
    float FalloffEnd;
    vec3 Position;
    float SpotPower;
};

#define MAX_LIGHTS 31

layout (set=0,binding=0) uniform PassCB {
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

const int MAX_MARCHING_STEPS = 100;
const float MIN_DIST = 0.001;
const float MAX_DIST = 100.0;
const float EPSILON = 0.0001;

// SDF for a sphere
float sdSphere(vec3 p, float s) {
    return length(p) - s;
}

// SDF for a plane
float sdPlane(vec3 p, vec3 n, float h) {
    return dot(p, n) + h;
}

// Smooth minimum
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// Scene SDF
float sceneSDF(vec3 p) {
    float sphere1 = sdSphere(p - vec3(0, 1, 0), 1.0);
    float sphere2 = sdSphere(p - vec3(2, 1, 2), 0.5);
    float plane = sdPlane(p, vec3(0, 1, 0), 0.0); // Ground plane

    float scene = smin(sphere1, plane, 0.2);
    scene = smin(scene, sphere2, 0.5);

    return scene;
}

// Calculate normal of the scene at a point p
vec3 calcNormal(vec3 p) {
    return normalize(vec3(
        sceneSDF(vec3(p.x + EPSILON, p.y, p.z)) - sceneSDF(vec3(p.x - EPSILON, p.y, p.z)),
        sceneSDF(vec3(p.x, p.y + EPSILON, p.z)) - sceneSDF(vec3(p.x, p.y - EPSILON, p.z)),
        sceneSDF(vec3(p.x, p.y, p.z + EPSILON)) - sceneSDF(vec3(p.x, p.y, p.z - EPSILON))
    ));
}

// Raymarch function
float rayMarch(vec3 ro, vec3 rd) {
    float dO = 0.0; // distance from origin
    for(int i = 0; i < MAX_MARCHING_STEPS; i++) {
        vec3 p = ro + rd * dO;
        float dS = sceneSDF(p);
        dO += dS;
        if(dO > MAX_DIST || dS < MIN_DIST) break;
    }
    return dO;
}

void main() {
    vec2 uv = inUV;

    // Create a ray
    vec4 target = invProj * vec4(uv.x * 2.0 - 1.0, (1.0-uv.y) * 2.0 - 1.0, 1.0, 1.0);
    vec3 rd = vec3(invView * vec4(normalize(target.xyz), 0.0));
    vec3 ro = gEyePosW;

    float dist = rayMarch(ro, rd);

    if (dist > MAX_DIST) {
        // Sky color
        outFragColor = vec4(0.5, 0.7, 1.0, 1.0);
        return;
    }

    vec3 p = ro + rd * dist;
    vec3 normal = calcNormal(p);

    // Simple lighting
    vec3 lightPos = vec3(5.0, 5.0, -5.0);
    vec3 lightDir = normalize(lightPos - p);
    float diff = max(dot(normal, lightDir), 0.1);

    vec3 color = vec3(0.8) * diff;

    outFragColor = vec4(color, 1.0);
}
