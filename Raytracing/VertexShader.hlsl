
struct vs_in {
    float4 pos : POSITION;
    float4 color : COLOR;
};

struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ConstantBuffer : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

vs_out main(vs_in input) {
    vs_out output;

    output.position = mul(mul(projection, view), mul(model, input.pos));
    output.color = input.color;

    return output;
}