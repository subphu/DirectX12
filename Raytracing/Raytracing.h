#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <string>    
#include <iostream>
#include <sstream>   
#include <vector>

#include <wrl.h>
#include <wrl/client.h>
#include <shellapi.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Raytracing {

public:
	Raytracing();
	Raytracing(HWND hwnd, UINT width, UINT height, std::wstring name);
	~Raytracing();

	void Init();
	void Destroy();
	void MainLoop();

	void KeyDown(UINT8 key);
	void KeyUp(UINT8 key);

private:

	struct Vertex {
		XMFLOAT3 pos;
		XMFLOAT4 color;
	};

	struct ConstantBuffer {
		XMMATRIX model;
		XMMATRIX view;
		XMMATRIX projection;
	};

	HWND m_hwnd;
	UINT m_width;
	UINT m_height;
	std::wstring m_title;
	float m_aspectRatio;

	static const UINT FrameCount = 2;


	UINT m_frameIndex;
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	ComPtr<ID3D12Device> m_device;
	ComPtr<IDXGISwapChain3> m_swapChain;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	UINT m_rtvDescriptorSize;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;
	HANDLE m_fenceEvent;

	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12PipelineState> m_pipelineState;

	ComPtr<ID3D12Resource> m_vertexBuffer;
	ComPtr<ID3D12Resource> m_indexBuffer;
	ComPtr<ID3D12Resource> m_depthStencilBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12DescriptorHeap> m_depthStencilHeap;

	ComPtr<ID3D12Resource> m_constantBuffer;
	UINT8* m_constantBufferLoc;

	void Update();
	void Render();

	void UpdateRenderPipeline();
	void WaitForPreviousFrame();
	void ExecuteRenderCommand();

	void CreateDevice(IDXGIFactory4* factory);
	void CreateSwapChain(IDXGIFactory4* factory);
	void CreateRTV();
	void CreateFence();
	void CreateRootSignature();
	void CreateGraphicsPSO();
	void CreateCommandList();

	void CreateInputBuffer();
	void CreateConstantBuffer();
	void CreateDepthStencilBuffer();
	void InitViewport();

	HRESULT CompileShader(LPCWSTR filename, LPCSTR target, D3D12_SHADER_BYTECODE* byteCode);
	void CreateBuffer(
		ID3D12Resource** buffer, int bufferSize,
		D3D12_HEAP_TYPE heapType, 
		D3D12_RESOURCE_STATES resourceStates,
		D3D12_RESOURCE_FLAGS dstFlags = D3D12_RESOURCE_FLAG_NONE);

	void BufferTransition(
		ID3D12Resource** srcBuffer,
		ID3D12Resource** dstBuffer,
		int bufferSize, BYTE* data,
		D3D12_RESOURCE_STATES dstStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	void CreateBufferTransition(int bufferSize, ID3D12Resource** dstBuffer, BYTE* data,
		D3D12_RESOURCE_FLAGS dstFlags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATES dstStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);


	DWORD indices[36] = {
		0, 1, 2,   0, 2, 3,
		4, 6, 5,   4, 7, 6,
		4, 5, 1,   4, 1, 0,
		3, 2, 6,   3, 6, 7,
		1, 5, 6,   1, 6, 2,
		4, 0, 3,   4, 3, 7
	};


	Vertex vertices[9] = {
		{ { -0.5f, -0.5f, -0.5f + 1.0f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ { -0.5f, +0.5f, -0.5f + 1.0f}, { 0.0f, 1.0f, 0.0f, 1.0f } },
		{ { +0.5f, +0.5f, -0.5f + 1.0f}, { 0.0f, 0.0f, 1.0f, 1.0f } },
		{ { +0.5f, -0.5f, -0.5f + 1.0f}, { 1.0f, 1.0f, 0.0f, 1.0f } },
		{ { -0.5f, -0.5f, +0.5f + 1.0f}, { 0.0f, 1.0f, 1.0f, 1.0f } },
		{ { -0.5f, +0.5f, +0.5f + 1.0f}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ { +0.5f, +0.5f, +0.5f + 1.0f}, { 1.0f, 0.0f, 1.0f, 1.0f } },
		{ { +0.5f, -0.5f, +0.5f + 1.0f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
	};

	enum GraphicsRootParameters : UINT32 {
		GraphicsRootCBV = 0,
		GraphicsRootSRVTable,
		GraphicsRootParametersCount
	};
};

