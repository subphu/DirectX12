
struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float4 locPos : POSITION;
};

float4 main(vs_out input) : SV_TARGET {
    float intensity = 0.5f - length(float2(0.5f, 0.5f) - input.locPos.xy);
    intensity = clamp(intensity, 0.0f, 0.5f) * 2.0f;
	return float4(1, 1, 1, 1);
}