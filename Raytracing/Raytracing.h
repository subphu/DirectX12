#pragma once

#include <Windowsx.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <string>    
#include <iostream>
#include <fstream>
#include <sstream>   
#include <vector>
#include <unordered_set>

#include <wrl.h>
#include <wrl/client.h>
#include <shellapi.h>

#include "Camera.h"

#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Raytracing {

public:
	Raytracing() {}
	Raytracing(HWND hwnd, UINT width, UINT height, std::wstring name);
	~Raytracing();

	void Init();
	void Destroy();
	void MainLoop();

	void KeyDown(UINT8 key);
	void KeyUp(UINT8 key);
	void MouseMove(UINT8 wParam, UINT32 lParam);
	void MouseWheel(float wParam);

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

	ConstantBuffer m_cbData;
	XMFLOAT2 m_mouse = { 0.f, 0.f };

	HWND m_hwnd;
	UINT m_width;
	UINT m_height;
	std::wstring m_title;
	float m_aspectRatio;

	bool m_raster = true;

	static const UINT FrameCount = 2;

	Camera m_camera;

	UINT m_frameIndex;
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	ComPtr<ID3D12Device5> m_device;
	ComPtr<IDXGISwapChain3> m_swapChain;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;

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
	
	UINT GetDebugFlag();
	void CheckRaytracingSupport();
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
	
	ID3D12Resource* CreateBuffer(int bufferSize, 
		D3D12_RESOURCE_STATES resourceStates,
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
	ID3D12Resource* CreateBufferTransition(int bufferSize, BYTE* data,
		D3D12_RESOURCE_FLAGS dstFlags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATES dstStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	UINT m_indicesCount = 36;
	DWORD m_indices[36] = {
		0, 1, 2,   0, 2, 3,
		4, 6, 5,   4, 7, 6,
		4, 5, 1,   4, 1, 0,
		3, 2, 6,   3, 6, 7,
		1, 5, 6,   1, 6, 2,
		4, 0, 3,   4, 3, 7
	};

	UINT m_verticesCount = 8;
	Vertex m_vertices[8] = {
		{ { -0.5f, -0.5f, -0.5f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ { -0.5f, +0.5f, -0.5f}, { 0.0f, 1.0f, 0.0f, 1.0f } },
		{ { +0.5f, +0.5f, -0.5f}, { 0.0f, 0.0f, 1.0f, 1.0f } },
		{ { +0.5f, -0.5f, -0.5f}, { 1.0f, 1.0f, 0.0f, 1.0f } },
		{ { -0.5f, -0.5f, +0.5f}, { 0.0f, 1.0f, 1.0f, 1.0f } },
		{ { -0.5f, +0.5f, +0.5f}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ { +0.5f, +0.5f, +0.5f}, { 1.0f, 0.0f, 1.0f, 1.0f } },
		{ { +0.5f, -0.5f, +0.5f}, { 1.0f, 0.0f, 0.0f, 1.0f } },
	};

	enum GraphicsRootParameters : UINT32 {
		GraphicsRootCBV = 0,
		//GraphicsRootSRVTable,
		GraphicsRootParametersCount
	};

	// #DXR
	struct AccelerationStructureBuffers	{
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};


	ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS

	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, XMMATRIX>> m_instances;


	AccelerationStructureBuffers CreateBottomLevelAS(
		ComPtr<ID3D12Resource> vertexBuffer, uint32_t vertexCount,
		ComPtr<ID3D12Resource> indexBuffer = nullptr, uint32_t indexCount = 0);
	void CreateTopLevelAS(
		const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances,
		bool updateOnly = false);
	void CreateAccelerationStructures();


	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();

	IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);
	void CreateRaytracingPipeline();

	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;

	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;

	ComPtr<ID3D12StateObject> m_rtStateObject;
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

	void CreateRaytracingOutputBuffer();
	void CreateShaderResourceHeap();
	ComPtr<ID3D12Resource> m_outputResource;
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

	void CreateShaderBindingTable();
	ComPtr<ID3D12Resource> m_sbtStorage;

	uint32_t m_rayGenEntrySize;
	uint32_t m_missEntrySize;
	uint32_t m_hitGroupEntrySize;
	uint32_t m_sbtSize;
};

