struct Particle {
    float4 pos;
};

struct vs_in {
    float4 pos : POSITION;
    float4 color : COLOR;
    uint id : SV_InstanceID;
};

struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ConstantBuffer : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float time;
};

StructuredBuffer<Particle> g_bufPos;

vs_out main(vs_in input) {
    vs_out output;

    output.position = mul(mul(projection, view), mul(model, input.pos) + g_bufPos[input.id].pos);
    output.color = input.color ;

	return output;
}