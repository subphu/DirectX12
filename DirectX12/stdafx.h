#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <string>      
#include <iostream>
#include <sstream>   
#include <vector>


#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }
#define KEY_W 0x57
#define KEY_A 0x41
#define KEY_S 0x53
#define KEY_D 0x44
#define KEY_E 0x45
#define KEY_Q 0x51
#define KEY_SPACE 0x20

using namespace DirectX;

struct Particle {
    XMFLOAT4 pos;
};

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT4 color;
};

struct ConstantBuffer {
    XMMATRIX model;
    XMMATRIX view;
    XMMATRIX projection;
};

HWND hwnd = NULL;
LPCTSTR WindowName = L"DirectX12";
LPCTSTR WindowTitle = L"DirectX12";
bool spaceKey = false;
bool prevSpaceKey = false;

int Width = 900;
int Height = 600;
bool FullScreen = false;
bool Running = true;

UINT particleCount = 10000;

float angle = 0.f;
float yaw = 0.f;
float pitch = 0.f;
float roll = 0.f;

POINT cursor;
float mouseZ = 0.f;

XMMATRIX camView;
XMMATRIX camProjection;

XMVECTOR camPosition;
XMVECTOR camTarget;
XMVECTOR camUp;
XMVECTOR camFront;
XMVECTOR camRight;

ConstantBuffer cbData;

// Direct3D
const int frameBufferCount = 2;
const int threadCount = 1;
const int fenceCount = frameBufferCount;

UINT frameIndex;
UINT rtvDescriptorSize;
ID3D12Device* device;
IDXGISwapChain3* swapChain;
ID3D12DescriptorHeap* rtvDescriptorHeap;
ID3D12Resource* renderTargets[frameBufferCount];
ID3D12CommandQueue* commandQueue;
ID3D12CommandAllocator* commandAllocator[frameBufferCount];
ID3D12GraphicsCommandList* commandList;
ID3D12Fence* fence[fenceCount];
HANDLE fenceEvent;
UINT64 fenceValue[fenceCount];

ID3D12PipelineState* pipelineStateObject;
ID3D12RootSignature* rootSignature;
D3D12_VIEWPORT viewport;
D3D12_RECT scissorRect;
ID3D12Resource* vertexBuffer; 
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
ID3D12Resource* indexBuffer;
D3D12_INDEX_BUFFER_VIEW indexBufferView;
ID3D12Resource* depthStencilBuffer; 
ID3D12DescriptorHeap* dsDescriptorHeap;

//ID3D12DescriptorHeap* mainDescriptorHeap[frameBufferCount]; // this heap will store the descripor to our constant buffer
//ID3D12Resource* constantBufferUploadHeap[frameBufferCount]; // this is the memory on the gpu where our constant buffer will be placed.
//UINT8* constantBufferGPUAddress[frameBufferCount];
ID3D12Resource* constantBuffer;
ID3D12Resource* constantBufferCS;
UINT8* constantBufferData;


void mainloop();
bool InitWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool fullscreen);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void RestartComputeBuffer();


bool InitD3D();
void Update();
void UpdatePipeline();
void Render();
void Cleanup();
void WaitForPreviousFrame();
void InitCamera();
void InitViewport();

void CreateDevice(IDXGIFactory4* dxgiFactory);
void CreateCommandQueue();
void CreateSwapChain(IDXGIFactory4* dxgiFactory);
void CreateRTV();
void CreateCommandList();
void CreateFence();
void CreateBufferTransition(int bufferSize, ID3D12Resource** dstBuffer, BYTE* data, 
    D3D12_RESOURCE_FLAGS dstFlag = D3D12_RESOURCE_FLAG_NONE,
    D3D12_RESOURCE_STATES dstStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
void CreateDepthStencilBuffer();
void CreateConstantBuffer();
HRESULT CreateGraphicsPipelineStateObj();

// Compute pipeline
ID3D12PipelineState* computeStateObject;
ID3D12RootSignature* computeRootSignature;

ID3D12DescriptorHeap* srvUavDescriptorHeap;
ID3D12Resource* particleBuffer0[threadCount];
ID3D12Resource* particleBuffer1[threadCount];
ID3D12Resource* particleBuffer0Upload[threadCount];
ID3D12Resource* particleBuffer1Upload[threadCount];
std::vector<Particle> particles;

UINT srvIndex[threadCount]; // Denotes which of the particle buffer resource views is the SRV (0 or 1). The UAV is 1 - srvIndex.
UINT srvUavDescriptorSize;

ID3D12CommandQueue* computeCommandQueue[threadCount];
ID3D12CommandAllocator* computeCommandAllocator[threadCount];
ID3D12GraphicsCommandList* computeCommandList[threadCount];

ID3D12Fence* computeFence[threadCount];
HANDLE computeFenceEvent[threadCount];
UINT64 computeFenceValue[threadCount];

HANDLE threadHandles[threadCount];
LONG volatile terminating;

void CreateComputeDescriptorHeap();
void CreateComputeRootSignature();
HRESULT CreateComputePipelineStateObj();
void CreateComputeCommandList();
void CreateComputeBuffer();
void UpdateComputePipeline(UINT threadIndex);

DWORD ComputeThread(int* threadIndex);

// Indices of the root signature parameters.
enum GraphicsRootParameters : UINT32 {
    GraphicsRootCBV = 0,
    GraphicsRootSRVTable,
    GraphicsRootParametersCount
};

enum ComputeRootParameters : UINT32 {
    ComputeRootCBV = 0,
    ComputeRootSRVTable,
    ComputeRootUAVTable,
    ComputeRootParametersCount
};

// Indices of shader resources in the descriptor heap.
enum DescriptorHeapIndex : UINT32 {
    UavParticle0 = 0,
    UavParticle1 = UavParticle0 + threadCount,
    SrvParticle0 = UavParticle1 + threadCount,
    SrvParticle1 = SrvParticle0 + threadCount,
    DescriptorCount = SrvParticle1 + threadCount
};


DWORD iList[] = {
    0, 1, 2,   0, 2, 3,
    4, 6, 5,   4, 7, 6,
    4, 5, 1,   4, 1, 0,
    3, 2, 6,   3, 6, 7,
    1, 5, 6,   1, 6, 2,
    4, 0, 3,   4, 3, 7
};


Vertex vList[] = {
    { { -0.5f, -0.5f, -0.5f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { -0.5f, +0.5f, -0.5f}, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { +0.5f, +0.5f, -0.5f}, { 0.0f, 0.0f, 1.0f, 1.0f } },
    { { +0.5f, -0.5f, -0.5f}, { 1.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f, +0.5f}, { 0.0f, 1.0f, 1.0f, 1.0f } },
    { { -0.5f, +0.5f, +0.5f}, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { { +0.5f, +0.5f, +0.5f}, { 1.0f, 0.0f, 1.0f, 1.0f } },
    { { +0.5f, -0.5f, +0.5f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
};