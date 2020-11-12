#include "Raytracing.h"

Raytracing::Raytracing(HWND hwnd, UINT width, UINT height, std::wstring name) {
    m_hwnd = hwnd;
	m_width = width;
	m_height = height;
	m_title = name;
	m_aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
    m_camera = Camera(XMVectorSet(0.0f, 3.0f, 5.0f, 0.0f), m_aspectRatio);
}

Raytracing::~Raytracing() { }

void Raytracing::MainLoop() {
    if (!m_fence) return;
    Update();
    Render();
}

void Raytracing::Update() {
    XMMATRIX model = XMMatrixIdentity();
    XMVECTOR rotAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX rotation = XMMatrixRotationAxis(rotAxis, XMConvertToRadians(0));
    XMMATRIX translation = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
    XMMATRIX scale = XMMatrixScaling(1.f, 1.f, 1.f);

    m_cbData.model = model * rotation * translation * scale;
    m_cbData.view = m_camera.GetView();
    m_cbData.projection = m_camera.GetProjection();

    UINT8* destination = m_constantBufferLoc + sizeof(ConstantBuffer) * m_frameIndex;
    memcpy(destination, &m_cbData, sizeof(ConstantBuffer));
}

void Raytracing::Render() {
    WaitForPreviousFrame();

    UpdateRenderPipeline();

    ExecuteRenderCommand();

    m_swapChain->Present(0, 0);
}

void Raytracing::UpdateRenderPipeline() {
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    m_commandList->SetGraphicsRootConstantBufferView(GraphicsRootCBV, m_constantBuffer->GetGPUVirtualAddress());
    
    D3D12_RESOURCE_BARRIER resourceBarrierToTarget = {};
    resourceBarrierToTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToTarget.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    resourceBarrierToTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    resourceBarrierToTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    resourceBarrierToTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &resourceBarrierToTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_depthStencilHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += size_t(m_frameIndex) * size_t(m_rtvDescriptorSize);

    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    if (m_raster) {
        const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->IASetIndexBuffer(&m_indexBufferView);

        m_commandList->DrawIndexedInstanced(m_indicesCount, 1, 0, 0, 0);
    } else {
        std::vector<ID3D12DescriptorHeap*> heaps = { m_srvUavHeap.Get() };
        m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

        D3D12_RESOURCE_BARRIER resourceBarrierToUav = {};
        resourceBarrierToUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierToUav.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierToUav.Transition.pResource = m_outputResource.Get();
        resourceBarrierToUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        resourceBarrierToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        resourceBarrierToUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &resourceBarrierToUav);

        D3D12_GPU_VIRTUAL_ADDRESS sbtAddress = m_sbtStorage->GetGPUVirtualAddress();
        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord.StartAddress = sbtAddress;
        desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenEntrySize;

        desc.MissShaderTable.StartAddress = sbtAddress + m_rayGenEntrySize;
        desc.MissShaderTable.SizeInBytes = m_missEntrySize;
        desc.MissShaderTable.StrideInBytes = m_missEntrySize;

        desc.HitGroupTable.StartAddress = sbtAddress + m_rayGenEntrySize + m_missEntrySize;
        desc.HitGroupTable.SizeInBytes = m_hitGroupEntrySize;
        desc.HitGroupTable.StrideInBytes = m_missEntrySize;

        desc.Width = m_width;
        desc.Height = m_height;
        desc.Depth = 1;

        m_commandList->SetPipelineState1(m_rtStateObject.Get());
        m_commandList->DispatchRays(&desc);

        D3D12_RESOURCE_BARRIER resourceBarrierToCopySrc = {};
        resourceBarrierToCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierToCopySrc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierToCopySrc.Transition.pResource = m_outputResource.Get();
        resourceBarrierToCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        resourceBarrierToCopySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        resourceBarrierToCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &resourceBarrierToCopySrc);

        D3D12_RESOURCE_BARRIER resourceBarrierToCopyDst = {};
        resourceBarrierToCopyDst.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierToCopyDst.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierToCopyDst.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        resourceBarrierToCopyDst.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resourceBarrierToCopyDst.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        resourceBarrierToCopyDst.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &resourceBarrierToCopyDst);

        m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputResource.Get());


        D3D12_RESOURCE_BARRIER resourceBarrierToTarget = {};
        resourceBarrierToTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierToTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierToTarget.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        resourceBarrierToTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        resourceBarrierToTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resourceBarrierToTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &resourceBarrierToTarget);


    }
    
    D3D12_RESOURCE_BARRIER resourceBarrierToPresent = {};
    resourceBarrierToPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToPresent.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToPresent.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    resourceBarrierToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    resourceBarrierToPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    resourceBarrierToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &resourceBarrierToPresent);

}

void Raytracing::WaitForPreviousFrame() {
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Raytracing::ExecuteRenderCommand() {
    m_commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
}

void Raytracing::KeyDown(UINT8 key) { }

void Raytracing::KeyUp(UINT8 key) {
    if (key == VK_ESCAPE) {
        PostQuitMessage(0);
    }
    if (key == VK_SPACE) {
        m_raster = !m_raster;
    }
}

void Raytracing::MouseMove(UINT8 wParam, UINT32 lParam) {
    bool lmb = wParam & MK_LBUTTON;
    bool mmb = wParam & MK_MBUTTON;
    bool rmb = wParam & MK_RBUTTON;
    if (!lmb) return;

    float x = float(GET_X_LPARAM(lParam));
    float y = float(GET_Y_LPARAM(lParam));

    m_camera.Move(x - m_mouse.x, y - m_mouse.y);
    m_mouse = { x, y };
}

void Raytracing::MouseWheel(float wParam) {
    m_camera.Zoom(GET_WHEEL_DELTA_WPARAM(wParam));
}

void Raytracing::Init() {
    std::wstringstream ss;
    ss << "Start" << "\n";
    OutputDebugString(ss.str().c_str());

	IDXGIFactory4* dxgiFactory;
	CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	CreateDevice(dxgiFactory);
	CreateSwapChain(dxgiFactory);
	CreateRTV();
    CreateFence();
    CreateRootSignature();
	CreateGraphicsPSO();
    CreateCommandList();

    CreateInputBuffer();
    CreateConstantBuffer();
    CreateDepthStencilBuffer();

    InitViewport();

    CheckRaytracingSupport();
    CreateAccelerationStructures();

    ExecuteRenderCommand();
    WaitForPreviousFrame();

    CreateRaytracingPipeline();
    CreateRaytracingOutputBuffer();
    CreateShaderResourceHeap();
    CreateShaderBindingTable();
}

void Raytracing::Destroy() {
    WaitForPreviousFrame();
    CloseHandle(m_fenceEvent);
}

void Raytracing::CheckRaytracingSupport() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
        throw std::runtime_error("Raytracing not supported on device");
}

void Raytracing::CreateDevice(IDXGIFactory4* factory) {
    IDXGIAdapter1* adapter;
    int adapterIndex = 0;
    bool adapterFound = false;

    while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapterIndex++;
            continue;
        }

        HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(hr)) {
            adapterFound = true;
            break;
        }

        adapterIndex++;
    }

    if (!adapterFound) return;
    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
}

void Raytracing::CreateSwapChain(IDXGIFactory4* factory) {
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    m_device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_commandQueue));

    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = m_width;
    backBufferDesc.Height = m_height;
    backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.BufferDesc = backBufferDesc;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = m_hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = true;

    IDXGISwapChain* swapChain;
    factory->CreateSwapChain(m_commandQueue.Get(), &swapChainDesc, &swapChain);

    m_swapChain = static_cast<IDXGISwapChain3*>(swapChain);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Raytracing::CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = FrameCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < FrameCount; i++) {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void Raytracing::CreateFence() {
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceValue = 0;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void Raytracing::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE1 descriptorTableRanges[1];
    descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorTableRanges[0].NumDescriptors = 1;
    descriptorTableRanges[0].BaseShaderRegister = 0;
    descriptorTableRanges[0].RegisterSpace = 0;
    descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    descriptorTableRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

    D3D12_ROOT_DESCRIPTOR_TABLE1 descriptorTable;
    descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
    descriptorTable.pDescriptorRanges = &descriptorTableRanges[0];

    D3D12_ROOT_DESCRIPTOR1 rootDesc;
    rootDesc.ShaderRegister = 0;
    rootDesc.RegisterSpace = 0;
    rootDesc.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    D3D12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount];
    rootParameters[GraphicsRootCBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[GraphicsRootCBV].Descriptor = rootDesc;
    rootParameters[GraphicsRootCBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //rootParameters[GraphicsRootSRVTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    //rootParameters[GraphicsRootSRVTable].DescriptorTable = descriptorTable;
    //rootParameters[GraphicsRootSRVTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signature;
    ID3DBlob* error;
    D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);

    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
}

void Raytracing::CreateGraphicsPSO() {
    D3D12_SHADER_BYTECODE vsBytecode = {};
    CompileShader(L"VertexShader.hlsl", "vs_5_0", &vsBytecode);
    D3D12_SHADER_BYTECODE psBytecode = {};
    CompileShader(L"PixelShader.hlsl", "ps_5_0", &psBytecode);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT   , 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    inputLayoutDesc.pInputElementDescs = inputLayout;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = TRUE;
    renderTargetBlendDesc.LogicOpEnable = FALSE;
    renderTargetBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTargetBlendDesc.DestBlend = D3D12_BLEND_ONE;
    renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ZERO;
    renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    renderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    renderTargetBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = renderTargetBlendDesc;

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = {};
    defaultStencilOp.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    defaultStencilOp.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    defaultStencilOp.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    defaultStencilOp.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencilDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthStencilDesc.FrontFace = defaultStencilOp;
    depthStencilDesc.BackFace = defaultStencilOp;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = vsBytecode;
    psoDesc.PS = psBytecode;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.BlendState = blendDesc;

    m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
}

void Raytracing::CreateCommandList() {
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));
}

HRESULT Raytracing::CompileShader(LPCWSTR filename, LPCSTR target, D3D12_SHADER_BYTECODE* byteCode) {
    ID3DBlob* vertexShader;
    ID3DBlob* errorBuff;
    HRESULT hr = D3DCompileFromFile(
        filename,
        nullptr, nullptr,
        "main", target,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &vertexShader, &errorBuff);

    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBuff->GetBufferPointer());
        return hr;
    }

    byteCode->BytecodeLength = vertexShader->GetBufferSize();
    byteCode->pShaderBytecode = vertexShader->GetBufferPointer();

    return hr;
}

ID3D12Resource* Raytracing::CreateBuffer(int bufferSize, D3D12_RESOURCE_STATES resourceStates, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = flags;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = heapType;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    ID3D12Resource* pBuffer;
    m_device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        resourceStates,
        nullptr,
        IID_PPV_ARGS(&pBuffer));

    return pBuffer;
}

ID3D12Resource* Raytracing::CreateBufferTransition(int bufferSize, BYTE* data, D3D12_RESOURCE_FLAGS dstFlags, D3D12_RESOURCE_STATES dstStates) {
    // create upload universal buffer
    ID3D12Resource* srcBuffer = CreateBuffer(
        bufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD);
    srcBuffer->SetName(L"Buffer Upload Resource Heap");

    // create default GPU buffer
    ID3D12Resource* dstBuffer = CreateBuffer(
        bufferSize,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_HEAP_TYPE_DEFAULT,
        dstFlags);
    dstBuffer->SetName(L"Buffer Default Resource Heap");

    // copy the data from the upload heap to the default heap
    BYTE* pData;
    srcBuffer->Map(0, NULL, reinterpret_cast<void**>(&pData));
    memcpy(pData, data, bufferSize);
    srcBuffer->Unmap(0, NULL);

    m_commandList->CopyBufferRegion(dstBuffer, 0, srcBuffer, 0, bufferSize);

    D3D12_RESOURCE_BARRIER resourceBarrier = {};
    resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrier.Transition.pResource = dstBuffer;
    resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    resourceBarrier.Transition.StateAfter = dstStates;
    resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(1, &resourceBarrier);
    return dstBuffer;
}

void Raytracing::CreateInputBuffer() {
    int vBufferSize = sizeof(m_vertices);
    m_vertexBuffer = CreateBufferTransition(vBufferSize, reinterpret_cast<BYTE*>(m_vertices));

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vBufferSize;

    int iBufferSize = sizeof(m_indices);
    m_indexBuffer = CreateBufferTransition(iBufferSize, reinterpret_cast<BYTE*>(m_indices));

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = iBufferSize;
}

void Raytracing::CreateConstantBuffer() {
    const UINT cBufferSize = sizeof(ConstantBuffer) * FrameCount;
    m_constantBuffer = CreateBuffer(
        cBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE);
    m_constantBuffer.Get()->SetName(L"Buffer Upload Resource Heap");

    D3D12_RANGE readRange = { 0, 0 };
    m_constantBuffer.Get()->Map(0, &readRange, reinterpret_cast<void**>(&m_constantBufferLoc));
    ZeroMemory(m_constantBufferLoc, cBufferSize);
}

void Raytracing::CreateDepthStencilBuffer() {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_depthStencilHeap));

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = m_width;
    resourceDesc.Height = m_height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 0;
    resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    m_device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&m_depthStencilBuffer)
    );

    m_depthStencilHeap->SetName(L"Depth/Stencil Resource Heap");

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

    m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &depthStencilDesc, m_depthStencilHeap->GetCPUDescriptorHandleForHeapStart());

}

void Raytracing::InitViewport() {
    m_viewport.TopLeftX = 0;
    m_viewport.TopLeftY = 0;
    m_viewport.Width = float(m_width);
    m_viewport.Height = float(m_height);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    m_scissorRect.left = 0;
    m_scissorRect.top = 0;
    m_scissorRect.right = m_width;
    m_scissorRect.bottom = m_height;
}

Raytracing::AccelerationStructureBuffers Raytracing::CreateBottomLevelAS(
    ComPtr<ID3D12Resource> vertexBuffer, uint32_t vertexCount,
    ComPtr<ID3D12Resource> indexBuffer, uint32_t indexCount) {
   
    D3D12_RAYTRACING_GEOMETRY_DESC rtGeometryDesc = {};
    rtGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    rtGeometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer->GetGPUVirtualAddress();
    rtGeometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    rtGeometryDesc.Triangles.VertexCount = vertexCount;
    rtGeometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    rtGeometryDesc.Triangles.IndexBuffer = indexBuffer ? indexBuffer->GetGPUVirtualAddress() : 0;
    rtGeometryDesc.Triangles.IndexFormat = indexBuffer ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
    rtGeometryDesc.Triangles.IndexCount = indexCount;
    rtGeometryDesc.Triangles.Transform3x4 = 0;
    rtGeometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc;
    prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    prebuildDesc.NumDescs = 1;
    prebuildDesc.pGeometryDescs = &rtGeometryDesc;
    prebuildDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

    // Buffer sizes need to be 256-byte-aligned
    UINT64 scratchSize = ROUND_UP(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    UINT64 resultSize = ROUND_UP(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    AccelerationStructureBuffers buffers;
    buffers.pScratch = CreateBuffer(
        static_cast<int>(scratchSize),
        D3D12_RESOURCE_STATE_COMMON, 
        D3D12_HEAP_TYPE_DEFAULT, 
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    buffers.pResult = CreateBuffer(
        static_cast<int>(resultSize),
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);


    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc;
    buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    buildDesc.Inputs.NumDescs = 1;
    buildDesc.Inputs.pGeometryDescs = &rtGeometryDesc;
    buildDesc.DestAccelerationStructureData = { buffers.pResult->GetGPUVirtualAddress() };
    buildDesc.ScratchAccelerationStructureData = { buffers.pScratch->GetGPUVirtualAddress() };
    buildDesc.SourceAccelerationStructureData = 0;
    buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    // Build the AS
    m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier;
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult.Get();
    uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    m_commandList->ResourceBarrier(1, &uavBarrier);

    return buffers;
}

void Raytracing::CreateTopLevelAS(
    const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances, 
    bool updateOnly) {
    UINT64 resultSize;
    UINT64 scratchSize;
    UINT64 instanceDescSize;
    if (!updateOnly) {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc = {};
        prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildDesc.NumDescs = static_cast<UINT>(instances.size());
        prebuildDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

        // Buffer sizes need to be 256-byte-aligned
        info.ResultDataMaxSizeInBytes = ROUND_UP(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        info.ScratchDataSizeInBytes = ROUND_UP(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        
        resultSize = info.ResultDataMaxSizeInBytes;
        scratchSize = info.ScratchDataSizeInBytes;
        instanceDescSize = ROUND_UP(
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(instances.size()),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        m_topLevelASBuffers.pScratch = CreateBuffer(
            static_cast<int>(scratchSize),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_topLevelASBuffers.pResult = CreateBuffer(
            static_cast<int>(resultSize),
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_topLevelASBuffers.pInstanceDesc = CreateBuffer(
            static_cast<int>(instanceDescSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE);
    }

    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
    m_topLevelASBuffers.pInstanceDesc->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));
    if (!instanceDescs) { throw std::logic_error("Cannot map the instance descriptor buffer - is it in the upload heap?"); }
    if (!updateOnly) { ZeroMemory(instanceDescs, instanceDescSize); }

    for (uint32_t i = 0; i < instances.size(); i++) {
        instanceDescs[i].InstanceID = static_cast<UINT>(i);
        instanceDescs[i].InstanceContributionToHitGroupIndex = static_cast<UINT>(2 * i);
        instanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        DirectX::XMMATRIX m = XMMatrixTranspose(instances[i].second);
        memcpy(instanceDescs[i].Transform, &m, sizeof(instanceDescs[i].Transform));
        instanceDescs[i].AccelerationStructure = instances[i].first->GetGPUVirtualAddress();
        instanceDescs[i].InstanceMask = 0xFF;
    }

    m_topLevelASBuffers.pInstanceDesc->Unmap(0, nullptr);

    D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = updateOnly ? m_topLevelASBuffers.pResult->GetGPUVirtualAddress() : 0;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    if (updateOnly) {
        flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    buildDesc.Inputs.InstanceDescs = m_topLevelASBuffers.pInstanceDesc->GetGPUVirtualAddress();
    buildDesc.Inputs.NumDescs = static_cast<UINT>(instances.size());
    buildDesc.DestAccelerationStructureData = { m_topLevelASBuffers.pResult->GetGPUVirtualAddress() };
    buildDesc.ScratchAccelerationStructureData = { m_topLevelASBuffers.pScratch->GetGPUVirtualAddress() };
    buildDesc.SourceAccelerationStructureData = pSourceAS;
    buildDesc.Inputs.Flags = flags;

    m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier;
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_topLevelASBuffers.pResult.Get();
    uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    m_commandList->ResourceBarrier(1, &uavBarrier);
}

void Raytracing::CreateAccelerationStructures() {
    AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS(
        m_vertexBuffer, m_verticesCount, m_indexBuffer, m_indicesCount);

    // Just one instance for now
    m_instances = { {bottomLevelBuffers.pResult, XMMatrixIdentity()} };
    CreateTopLevelAS(m_instances);

    // Store the AS buffers. The rest of the buffers will be released once we exit the function
    m_bottomLevelAS = bottomLevelBuffers.pResult;
}

ComPtr<ID3D12RootSignature> Raytracing::CreateRayGenSignature() {

    D3D12_DESCRIPTOR_RANGE rangeUav = {};
    rangeUav.BaseShaderRegister = 0;
    rangeUav.NumDescriptors = 1;
    rangeUav.RegisterSpace = 0;
    rangeUav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeUav.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE rangeSrv {};
    rangeSrv.BaseShaderRegister = 0;
    rangeSrv.NumDescriptors = 1;
    rangeSrv.RegisterSpace = 0;
    rangeSrv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeSrv.OffsetInDescriptorsFromTableStart = 1;

    std::vector<D3D12_DESCRIPTOR_RANGE> rangeStorage = { rangeUav, rangeSrv };

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 2;
    param.DescriptorTable.pDescriptorRanges = rangeStorage.data();

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ID3DBlob* pSigBlob;
    ID3DBlob* pErrorBlob;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pSigBlob, &pErrorBlob);

    ID3D12RootSignature* pRootSig;
    m_device->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    return pRootSig;
}

ComPtr<ID3D12RootSignature> Raytracing::CreateMissSignature() {
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 0;
    rootDesc.pParameters = {};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ID3DBlob* pSigBlob;
    ID3DBlob* pErrorBlob;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pSigBlob, &pErrorBlob);

    ID3D12RootSignature* pRootSig;
    m_device->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    return pRootSig;
}

ComPtr<ID3D12RootSignature> Raytracing::CreateHitSignature() {
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    param.Descriptor.RegisterSpace = 0;
    param.Descriptor.ShaderRegister = 1;

    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ID3DBlob* pSigBlob;
    ID3DBlob* pErrorBlob;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pSigBlob, &pErrorBlob);

    ID3D12RootSignature* pRootSig;
    m_device->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    return pRootSig;
}

IDxcBlob* Raytracing::CompileShaderLibrary(LPCWSTR fileName) {
    static IDxcCompiler* pCompiler = nullptr;
    static IDxcLibrary* pLibrary = nullptr;
    static IDxcIncludeHandler* dxcIncludeHandler;

    HRESULT hr;

    // Initialize the DXC compiler and compiler helper
    DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void**)&pCompiler);
    DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)&pLibrary);
    pLibrary->CreateIncludeHandler(&dxcIncludeHandler);

    // Open and read the file
    std::ifstream shaderFile(fileName);
    if (shaderFile.good() == false) { throw std::logic_error("Cannot find shader file"); }
    std::stringstream strStream;
    strStream << shaderFile.rdbuf();
    std::string sShader = strStream.str();

    // Create blob from the string
    IDxcBlobEncoding* pTextBlob;
    pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)sShader.c_str(), (uint32_t)sShader.size(), 0, &pTextBlob);
    if (!pTextBlob) { throw std::logic_error("Cannot create blob file"); }

    // Compile
    IDxcOperationResult* pResult;
    pCompiler->Compile(
        pTextBlob, fileName, 
        L"", L"lib_6_3",
        nullptr, 0,
        nullptr, 0, 
        dxcIncludeHandler, &pResult);

    // Verify the result
    HRESULT resultCode;
    pResult->GetStatus(&resultCode);
    if (FAILED(resultCode)) {
        IDxcBlobEncoding* pError;
        hr = pResult->GetErrorBuffer(&pError);
        if (FAILED(hr)) { throw std::logic_error("Failed to get shader compiler error"); }

        // Convert error blob to a string
        std::vector<char> infoLog(pError->GetBufferSize() + 1);
        memcpy(infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize());
        infoLog[pError->GetBufferSize()] = 0;

        std::string errorMsg = "Shader Compiler Error:\n";
        errorMsg.append(infoLog.data());

        MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
        throw std::logic_error("Failed compile shader");
    }

    IDxcBlob* pBlob;
    pResult->GetResult(&pBlob);
    return pBlob;
}

void Raytracing::CreateRaytracingPipeline() {
    UINT64 subobjectCount =
        3 +     // DXIL libraries
        1 +     // Hit group declarations
        1 +     // Shader configuration
        1 +     // Shader payload
        2 * 3 + // Root signature declaration + association
        2 +     // Empty global and local root signatures
        1;      // Final pipeline subobject

    std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);

    UINT currentIndex = 0;


    m_rayGenLibrary = CompileShaderLibrary(L"RayGen.hlsl");
    m_missLibrary = CompileShaderLibrary(L"Miss.hlsl");
    m_hitLibrary = CompileShaderLibrary(L"Hit.hlsl");

    D3D12_EXPORT_DESC rayGenExportDesc = {};
    rayGenExportDesc.Name = L"RayGen";
    rayGenExportDesc.ExportToRename = nullptr;
    rayGenExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

    D3D12_DXIL_LIBRARY_DESC rayGenLibDesc;
    rayGenLibDesc.DXILLibrary.BytecodeLength = m_rayGenLibrary->GetBufferSize();
    rayGenLibDesc.DXILLibrary.pShaderBytecode = m_rayGenLibrary->GetBufferPointer();
    rayGenLibDesc.NumExports = 1;
    rayGenLibDesc.pExports = &rayGenExportDesc;

    D3D12_STATE_SUBOBJECT rayGenLibSubobject = {};
    rayGenLibSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    rayGenLibSubobject.pDesc = &rayGenLibDesc;
    subobjects[currentIndex++] = rayGenLibSubobject;

    D3D12_EXPORT_DESC missExportDesc = { L"Miss" , nullptr, D3D12_EXPORT_FLAG_NONE };
    D3D12_DXIL_LIBRARY_DESC missLibDesc = { {m_missLibrary->GetBufferPointer(), m_missLibrary->GetBufferSize()}, 1, &missExportDesc };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &missLibDesc };

    D3D12_EXPORT_DESC hitExportDesc = { L"ClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE };
    D3D12_DXIL_LIBRARY_DESC hitLibDesc = { {m_hitLibrary->GetBufferPointer(), m_hitLibrary->GetBufferSize()}, 1, &hitExportDesc };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &hitLibDesc };


    D3D12_HIT_GROUP_DESC desc = {};
    desc.HitGroupExport = L"HitGroup";
    desc.ClosestHitShaderImport = L"ClosestHit";
    desc.AnyHitShaderImport = nullptr;
    desc.IntersectionShaderImport = nullptr;

    D3D12_STATE_SUBOBJECT hitGroup = {};
    hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroup.pDesc = &desc;
    subobjects[currentIndex++] = hitGroup;


    D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
    shaderDesc.MaxPayloadSizeInBytes = 4 * sizeof(float);
    shaderDesc.MaxAttributeSizeInBytes = 2 * sizeof(float);

    D3D12_STATE_SUBOBJECT shaderConfigObject = {};
    shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigObject.pDesc = &shaderDesc;
    subobjects[currentIndex++] = shaderConfigObject;


    std::vector<std::wstring> exportedSymbols = { L"RayGen", L"Miss", L"HitGroup"}; // ClosestHit removed
    std::vector<LPCWSTR> exportedSymbolPointers = {};

    // Build an array of the string pointers
    exportedSymbolPointers.reserve(exportedSymbols.size());
    for (const auto& name : exportedSymbols) {
        exportedSymbolPointers.push_back(name.c_str());
    }

    // Add a subobject for the association between shaders and the payload
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
    shaderPayloadAssociation.NumExports = static_cast<UINT>(exportedSymbols.size());
    shaderPayloadAssociation.pExports = exportedSymbolPointers.data();

    // Associate the set of shaders with the payload defined in the previous subobject
    shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(currentIndex - 1)];

    // Create and store the payload association object
    D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
    shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;
    subobjects[currentIndex++] = shaderPayloadAssociationObject;

    m_rayGenSignature = CreateRayGenSignature();
    m_missSignature   = CreateMissSignature();
    m_hitSignature    = CreateHitSignature();

    ID3D12RootSignature* rayGenSignature = m_rayGenSignature.Get();
    ID3D12RootSignature* missSignature = m_missSignature.Get();
    ID3D12RootSignature* hitSignature = m_hitSignature.Get();

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &rayGenSignature };
    LPCWSTR rayGenSymbolPointers = L"RayGen" ;
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenAssociation = {};
    rayGenAssociation.NumExports = 1;
    rayGenAssociation.pExports = &rayGenSymbolPointers;
    rayGenAssociation.pSubobjectToAssociate = &subobjects[currentIndex - 1];
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &rayGenAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &missSignature };
    LPCWSTR missSymbolPointers = L"Miss";
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missAssociation = { &subobjects[currentIndex - 1], 1, &missSymbolPointers };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &missAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &hitSignature };
    LPCWSTR hitSymbolPointers = L"ClosestHit";
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION hitAssociation = { &subobjects[currentIndex - 1], 1, &hitSymbolPointers };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &hitAssociation };


    ID3DBlob* serializedRootSignature;
    ID3DBlob* error;

    ID3D12RootSignature* globalRootSignature;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = { 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSignature, &error);

    m_device->CreateRootSignature(0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(&globalRootSignature));
    serializedRootSignature->Release();

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRootSignature };

    ID3D12RootSignature* localRootSignature;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSignature, &error);

    m_device->CreateRootSignature(0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(&localRootSignature));
    serializedRootSignature->Release();

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &localRootSignature };

    // Add a subobject for the ray tracing pipeline configuration
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
    pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigObject.pDesc = &pipelineConfig;

    subobjects[currentIndex++] = pipelineConfigObject;

    // Describe the ray tracing pipeline state object
    D3D12_STATE_OBJECT_DESC pipelineDesc = {};
    pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipelineDesc.NumSubobjects = currentIndex;
    pipelineDesc.pSubobjects = subobjects.data();


    HRESULT hr = m_device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&m_rtStateObject));
    if (FAILED(hr)) { throw std::logic_error("Could not create the raytracing state object"); }
    m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps));
}

void Raytracing::CreateRaytracingOutputBuffer() {
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resourceDesc.Width = m_width;
    resourceDesc.Height = m_height;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    m_device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        nullptr,
        IID_PPV_ARGS(&m_outputResource));
}

void Raytracing::CreateShaderResourceHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 2;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvUavHeap));

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc, srvHandle);

    srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
    m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

void Raytracing::CreateShaderBindingTable() {
    // A SBT entry is made of a program ID and a set of parameters, taking 8 bytes each. 
    UINT m_progIdSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    m_rayGenEntrySize   = ROUND_UP(m_progIdSize + 8 * 1, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    m_missEntrySize     = ROUND_UP(m_progIdSize + 8 * 0, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT); // no param
    m_hitGroupEntrySize = ROUND_UP(m_progIdSize + 8 * 1, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    m_sbtSize           = ROUND_UP(m_rayGenEntrySize + m_missEntrySize + m_hitGroupEntrySize, 256);

    uint8_t* pData;
    m_sbtStorage = CreateBuffer(m_sbtSize, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    m_sbtStorage->Map(0, nullptr, reinterpret_cast<void**>(&pData));

    auto heapPointer = reinterpret_cast<UINT64*>(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr);
    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"RayGen"), m_progIdSize); // Copy the shader identifier
    memcpy(pData + m_progIdSize, &heapPointer, 8 * 1); // Copy all its resources pointers or values in bulk
    pData += m_rayGenEntrySize;

    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"Miss"), m_progIdSize);
    pData += m_missEntrySize;

    auto vertexHeapPointer = { (void*)(m_vertexBuffer->GetGPUVirtualAddress()) };
    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"HitGroup"), m_progIdSize);
    memcpy(pData + m_progIdSize, &vertexHeapPointer, 8 * 1);

    m_sbtStorage->Unmap(0, nullptr);
}
