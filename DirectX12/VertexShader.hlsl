struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ConstantBuffer : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

StructuredBuffer<float4> g_bufPos;

vs_out main( float4 pos : POSITION, float4 color : COLOR) {
    vs_out output;

    output.position = mul(mul(mul(projection, view), model), pos);
    output.color = color;

	return output;
}