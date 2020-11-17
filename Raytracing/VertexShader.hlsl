
struct vs_in {
    float4 pos : POSITION;
    float4 color : COLOR;
};

struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct InstanceData { float4x4 model; };

cbuffer ConstantBuffer : register(b0) {
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;
};

uint instanceIdx : register(b1);

StructuredBuffer<InstanceData> instance : register(t0);

vs_out main(vs_in input) {
    vs_out output;

    float4x4 model = instance[instanceIdx].model;
    output.position = mul(mul(projection, view), mul(model, input.pos));
    //output.position = mul(mul(projection, view), input.pos);
    output.color = input.color;

    return output;
}