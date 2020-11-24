struct HitInfo { float4 colorAndDistance; };
struct Attributes { float2 bary; };

RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

cbuffer CameraParams : register(b0) {
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;
}

[shader("raygeneration")] void RayGen() {
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);

    float2 dimensions = float2(DispatchRaysDimensions().xy);
    float aspectRatio = dimensions.x / dimensions.y;
    uint2 rayIndex = DispatchRaysIndex().xy;
    float2 coord = (rayIndex.xy / dimensions.xy) * 2.f - 1.f; // normalized -1 to 1

    RayDesc ray;
    ray.Origin = mul(viewI, float4(0, 0, 0, 1));
    float4 target = mul(projectionI, float4(coord.x, -coord.y, 1, 1));
    ray.Direction = mul(viewI, float4(target.xyz, 0));
    ray.TMin = 0;
    ray.TMax = 100000;

    TraceRay(
        SceneBVH,
        RAY_FLAG_NONE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload);
    gOutput[rayIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
