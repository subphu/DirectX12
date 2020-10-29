
#define blocksize 128

struct Particle {
	float4 pos;
};

cbuffer ConstantBuffer : register(b0) {
	float4x4 model;
	float4x4 view;
	float4x4 projection;
	float time;
};

StructuredBuffer<Particle> oldPos      : register(t0);    // SRV
RWStructuredBuffer<Particle> newPos    : register(u0);    // UAV

[numthreads(blocksize, 1, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex) {
	newPos[DTid.x].pos += oldPos[DTid.x].pos * 0.0004;

}