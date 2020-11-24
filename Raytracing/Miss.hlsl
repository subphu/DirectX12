struct HitInfo { float4 colorAndDistance; };

[shader("miss")] 
void Miss(inout HitInfo payload : SV_RayPayload) {
    uint2 rayIndex = DispatchRaysIndex().xy;
    float2 dimension = float2(DispatchRaysDimensions().xy);

    float gradient = (dimension.y - rayIndex.y) / dimension.y;
    float3 background = float3(0.4f, 0.4f, 0.4f) * gradient;
    payload.colorAndDistance = float4(background, -1.0f);
}