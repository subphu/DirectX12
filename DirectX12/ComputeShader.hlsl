
#define blocksize 128
groupshared float4 sharedPos[blocksize];

StructuredBuffer<float4> oldPos      : register(t0);    // SRV
RWStructuredBuffer<float4> newPos    : register(u0);    // UAV

[numthreads(blocksize, 1, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex) {
	newPos[DTid.x].y = oldPos[DTid.x].y - 0.01;
}