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

#include "d3dx12.h"

#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }

HWND hwnd = NULL;
LPCTSTR WindowName = L"DirectX12";
LPCTSTR WindowTitle = L"DirectX12";
int Width = 900;
int Height = 600;
bool FullScreen = false;
bool Running = true;

void mainloop();
bool InitWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool fullscreen);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Direct3D
const int frameBufferCount = 3;
const int threadCount = 1;
const int commandAllocatorCount = frameBufferCount * threadCount;
const int fenceCount = commandAllocatorCount;

ID3D12Device* device;
IDXGISwapChain3* swapChain;
ID3D12DescriptorHeap* rtvDescriptorHeap;
ID3D12Resource* renderTargets[frameBufferCount];
ID3D12CommandQueue* commandQueue;
ID3D12CommandAllocator* commandAllocator[commandAllocatorCount];
ID3D12GraphicsCommandList* commandList;
ID3D12Fence* fence[fenceCount];
HANDLE fenceEvent;
UINT64 fenceValue[fenceCount];
int frameIndex;
int rtvDescriptorSize;

bool InitD3D();
void Update();
void UpdatePipeline();
void Render();
void Cleanup();
void WaitForPreviousFrame();