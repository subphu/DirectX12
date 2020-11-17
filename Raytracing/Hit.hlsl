#include "Common.hlsl"

struct ShadowHitInfo {
    bool isHit;
};

RaytracingAccelerationStructure SceneBVH : register(t2);

struct STriVertex {
    float3 vertex;
    float4 color;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")] void ClosestHit(inout HitInfo payload,
    Attributes attrib) {
    float3 barycentrics =
        float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    uint vertId = 3 * PrimitiveIndex();
    // #DXR Extra: Per-Instance Data
    float3 hitColor = float3(0.6, 0.7, 0.6);
    // Shade only the first 3 instances (triangles)
    if (InstanceID() < 3) {

        // #DXR Extra: Per-Instance Data
        hitColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
            BTriVertex[indices[vertId + 1]].color * barycentrics.y +
            BTriVertex[indices[vertId + 2]].color * barycentrics.z;
    }

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}

// #DXR Extra - Another ray type
[shader("closesthit")] void PlaneClosestHit(inout HitInfo payload,
    Attributes attrib) {
    float3 lightPos = float3(2, 2, -2);

    // Find the world - space hit position
    float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 lightDir = normalize(lightPos - worldOrigin);

    // Fire a shadow ray. The direction is hard-coded here, but can be fetched
    // from a constant-buffer
    RayDesc ray;
    ray.Origin = worldOrigin;
    ray.Direction = lightDir;
    ray.TMin = 0.01;
    ray.TMax = 100000;
    bool hit = true;

    // Initialize the ray payload
    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = false;

    TraceRay(
        SceneBVH,
        RAY_FLAG_NONE,
        0xFF,
        1,
        0,
        1,
        ray,
        shadowPayload);

    float factor = shadowPayload.isHit ? 0.3 : 1.0;

    float3 barycentrics =
        float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    float4 hitColor = float4(float3(0.7, 0.7, 0.3) * factor, RayTCurrent());
    payload.colorAndDistance = float4(hitColor);
}