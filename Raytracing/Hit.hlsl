struct HitInfo { float4 colorAndDistance; };
struct Attributes { float2 bary; };
struct Vertex { float3 pos; float4 color; };

StructuredBuffer<Vertex> vertices : register(t0);
StructuredBuffer<int> indices : register(t1);

struct ShadowHitInfo { bool isHit; };
RaytracingAccelerationStructure SceneBVH : register(t2);

static const float3 lightColor = float3(0.9, 0.9, 0.85);
static const float3 lightPos = float3(2.0f, 3.0f, 4.0f);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float3 lightDir = normalize(lightPos - worldOrigin);

    RayDesc ray;
    ray.Origin = worldOrigin;
    ray.Direction = lightDir;
    ray.TMin = 0.01;
    ray.TMax = 100000;
    bool hit = true;

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

    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    uint startIdx = 3 * PrimitiveIndex();
    float3 hitColor = vertices[indices[startIdx + 0]].color * barycentrics.x +
                      vertices[indices[startIdx + 1]].color * barycentrics.y +
                      vertices[indices[startIdx + 2]].color * barycentrics.z;

    payload.colorAndDistance = float4(lightColor * hitColor * factor, RayTCurrent());
}

[shader("closesthit")] 
void PlaneClosestHit(inout HitInfo payload, Attributes attrib) {
    float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float3 lightDir = normalize(lightPos - worldOrigin);

    RayDesc ray;
    ray.Origin = worldOrigin;
    ray.Direction = lightDir;
    ray.TMin = 0.01;
    ray.TMax = 100000;
    bool hit = true;

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

    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    payload.colorAndDistance = float4(lightColor * factor, RayTCurrent());
}