
struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(vs_out input) : SV_TARGET {
    return float4(1,1,1,1);
}