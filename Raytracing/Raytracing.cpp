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
    XMMATRIX translation = XMMatrixTranslation(0.0f, -1.f, 0.0f);
    XMMATRIX scale = XMMatrixScaling(4.f, 4.f, 4.f);
    m_instances[1].second = scale * translation * rotation * model;

    XMVECTOR det;
    m_cbData.view = m_camera.GetView();
    m_cbData.projection = m_camera.GetProjection();
    m_cbData.viewI = XMMatrixInverse(&det, m_camera.GetView());
    m_cbData.projectionI = XMMatrixInverse(&det, m_camera.GetProjection());

    // update constant data
    uint8_t* pData;
    m_constantBuffer->Map(0, nullptr, (void**)&pData);
    memcpy(pData, &m_cbData, sizeof(ConstantBuffer));
    m_constantBuffer->Unmap(0, nullptr);

    // update instance data
    InstanceData* current = nullptr;
    D3D12_RANGE range = { 0, 0 };
    m_instanceBuffer->Map(0, &range, reinterpret_cast<void**>(&current));
    for (const auto& inst : m_instances) {
        current->model = inst.second;
        current++;
    }
    m_instanceBuffer->Unmap(0, nullptr);
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

    if (m_raster) {
        const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        std::vector<ID3D12DescriptorHeap*> heaps = { m_cbvSrvHeap.Get() };
        m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        m_commandList->SetGraphicsRootDescriptorTable(0, handle); // CBV
        m_commandList->SetGraphicsRootDescriptorTable(1, handle); // SRV
        m_commandList->SetGraphicsRoot32BitConstant(2, 0, 0);

        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->IASetIndexBuffer(&m_indexBufferView);
        m_commandList->DrawIndexedInstanced(m_indicesCount, 1, 0, 0, 0);

        m_commandList->SetGraphicsRoot32BitConstant(2, 1, 0);

        m_commandList->IASetVertexBuffers(0, 1, &m_planeBufferView);
        m_commandList->DrawInstanced(m_planeVertCount, 1, 0, 0);
    } else {
        CreateTopLevelAS(m_instances, true);

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
        desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenSectionSize;

        desc.MissShaderTable.StartAddress = sbtAddress + m_rayGenSectionSize;
        desc.MissShaderTable.SizeInBytes = m_missSectionSize;
        desc.MissShaderTable.StrideInBytes = m_missEntrySize;

        desc.HitGroupTable.StartAddress = sbtAddress + m_rayGenSectionSize + m_missSectionSize;
        desc.HitGroupTable.SizeInBytes = m_hitGroupSectionSize;
        desc.HitGroupTable.StrideInBytes = m_hitGroupEntrySize;

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
    float x = float(GET_X_LPARAM(lParam));
    float y = float(GET_Y_LPARAM(lParam));
    XMFLOAT2 mouse = m_mouse;
    m_mouse = { x, y };
    if (!lmb) return;

    m_camera.Move(x - mouse.x, y - mouse.y);
}

void Raytracing::MouseWheel(float wParam) {
    m_camera.Zoom(GET_WHEEL_DELTA_WPARAM(wParam));
}

void Raytracing::Init() {
    std::wstringstream ss;
    ss << "Start" << "\n";
    OutputDebugString(ss.str().c_str());

	IDXGIFactory4* dxgiFactory;
    CreateDXGIFactory2(GetDebugFlag(), IID_PPV_ARGS(&dxgiFactory));
	CreateDevice(dxgiFactory);
	CreateSwapChain(dxgiFactory);
	CreateRTV();
    CreateFence();
    CreateRootSignature();
	CreateGraphicsPSO();
    CreateCommandList();

    CreateInputBuffer();

    CheckRaytracingSupport();
    CreateAccelerationStructures();
    
    CreateDepthStencilBuffer();
    CreateConstantBuffer();
    CreateInstanceBuffer();
    CreateCbvSrvHeap();

    InitViewport();

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

UINT Raytracing::GetDebugFlag() {
    UINT dxgiFactoryFlags = 0;
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();

        // Enable additional debug layers.
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    return dxgiFactoryFlags;
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

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    IDXGISwapChain1* swapChain;
    factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain);

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
    D3D12_DESCRIPTOR_RANGE1 ranges[ParametersCount - 1];
    ranges[IdxCBV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[IdxCBV].NumDescriptors = 1;
    ranges[IdxCBV].BaseShaderRegister = 0;
    ranges[IdxCBV].RegisterSpace = 0;
    ranges[IdxCBV].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[IdxCBV].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
           
    ranges[IdxSRV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[IdxSRV].NumDescriptors = 1;
    ranges[IdxSRV].BaseShaderRegister = 0;
    ranges[IdxSRV].RegisterSpace = 0;
    ranges[IdxSRV].OffsetInDescriptorsFromTableStart = 1; //
    ranges[IdxSRV].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

    D3D12_ROOT_DESCRIPTOR_TABLE1 descriptorTables[ParametersCount - 1];
    descriptorTables[IdxCBV].NumDescriptorRanges = 1;
    descriptorTables[IdxCBV].pDescriptorRanges = &ranges[0];
                    
    descriptorTables[IdxSRV].NumDescriptorRanges = 1;
    descriptorTables[IdxSRV].pDescriptorRanges = &ranges[1];

    D3D12_ROOT_CONSTANTS rootConstants;
    rootConstants.Num32BitValues = 1;
    rootConstants.ShaderRegister = 1;
    rootConstants.RegisterSpace = 0;

    D3D12_ROOT_PARAMETER1 rootParameters[ParametersCount];
    rootParameters[IdxCBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[IdxCBV].DescriptorTable = descriptorTables[0];
    rootParameters[IdxCBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[IdxSRV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[IdxSRV].DescriptorTable = descriptorTables[1];
    rootParameters[IdxSRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[IdxInstance].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[IdxInstance].Constants = rootConstants;
    rootParameters[IdxInstance].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    m_vertexBuffer = CreateBuffer(
        vBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD);
    m_vertexBuffer->SetName(L"Vertex Buffer");

    D3D12_RANGE range = { 0, 0 };
    UINT8* pVertexDataBegin;
    m_vertexBuffer->Map(0, &range, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, m_vertices, vBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vBufferSize;

    int iBufferSize = sizeof(m_indices);
    m_indexBuffer = CreateBuffer(
        iBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD);
    m_indexBuffer->SetName(L"Index Buffer");

    UINT8* pIndexDataBegin;
    m_indexBuffer->Map(0, &range, reinterpret_cast<void**>(&pIndexDataBegin));
    memcpy(pIndexDataBegin, m_indices, iBufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = iBufferSize;

    // Plane
    int pBufferSize = sizeof(m_planeVertices);
    m_planeBuffer = CreateBuffer(
        pBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD);
    m_planeBuffer->SetName(L"Plane Vertex Buffer");

    UINT8* pPlaneDataBegin;
    m_planeBuffer->Map(0, &range, reinterpret_cast<void**>(&pPlaneDataBegin));
    memcpy(pPlaneDataBegin, m_planeVertices, pBufferSize);
    m_planeBuffer->Unmap(0, nullptr);

    m_planeBufferView.BufferLocation = m_planeBuffer->GetGPUVirtualAddress();
    m_planeBufferView.StrideInBytes = sizeof(Vertex);
    m_planeBufferView.SizeInBytes = pBufferSize;

}

void Raytracing::CreateConstantBuffer() {
    const UINT cBufferSize = sizeof(ConstantBuffer);
    m_constantBuffer = CreateBuffer(
        cBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE);
    m_constantBuffer.Get()->SetName(L"Constant Buffer Upload Resource Heap");
}

void Raytracing::CreateInstanceBuffer() {
    uint32_t iBufferSize = ROUND_UP(
        static_cast<uint32_t>(m_instances.size()) * sizeof(InstanceData),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    m_instanceBuffer = CreateBuffer(
        iBufferSize,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE);
    m_instanceBuffer.Get()->SetName(L"Instance Buffer Upload Resource Heap");
}

void Raytracing::CreateCbvSrvHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = 2;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(ConstantBuffer);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateConstantBufferView(&cbvDesc, handle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(m_instances.size());
    srvDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    handle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, handle);
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
    buffers.pScratch->SetName(L"Buffer Scratch");
    buffers.pResult = CreateBuffer(
        static_cast<int>(resultSize),
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    buffers.pResult->SetName(L"Buffer Result");


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
        m_topLevelASBuffers.pScratch->SetName(L"Top Level Buffer Scratch");
        m_topLevelASBuffers.pResult = CreateBuffer(
            static_cast<int>(resultSize),
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_topLevelASBuffers.pResult->SetName(L"Top Level Buffer Scratch");

        m_topLevelASBuffers.pInstanceDesc = CreateBuffer(
            static_cast<int>(instanceDescSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE);
        m_topLevelASBuffers.pInstanceDesc->SetName(L"Top Level Buffer Instance");
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

    AccelerationStructureBuffers planeBottomLevelBuffers =
        CreateBottomLevelAS(m_planeBuffer, m_planeVertCount);

    m_instances = { 
        { bottomLevelBuffers.pResult, XMMatrixIdentity() },
        { planeBottomLevelBuffers.pResult, XMMatrixIdentity() },
    };
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

    D3D12_DESCRIPTOR_RANGE rangeSrv = {};
    rangeSrv.BaseShaderRegister = 0;
    rangeSrv.NumDescriptors = 1;
    rangeSrv.RegisterSpace = 0;
    rangeSrv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeSrv.OffsetInDescriptorsFromTableStart = 1;

    D3D12_DESCRIPTOR_RANGE rangeCbv = {};
    rangeCbv.BaseShaderRegister = 0;
    rangeCbv.NumDescriptors = 1;
    rangeCbv.RegisterSpace = 0;
    rangeCbv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    rangeCbv.OffsetInDescriptorsFromTableStart = 2;

    std::vector<D3D12_DESCRIPTOR_RANGE> rangeStorage = { rangeUav, rangeSrv, rangeCbv };

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 3;
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
    std::vector<D3D12_ROOT_PARAMETER> params = { {}, {}, {}, {} };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
         
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE range = {};
    range.BaseShaderRegister = 2;
    range.NumDescriptors = 1;
    range.RegisterSpace = 0;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.OffsetInDescriptorsFromTableStart = 1;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &range;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = params.size();
    rootDesc.pParameters = params.data();
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
        4 +     // DXIL libraries
        3 +     // Hit group declarations
        1 +     // Shader configuration
        1 +     // Shader payload
        2 * 6 + // Root signature declaration + association
        2 +     // Empty global and local root signatures
        1;      // Final pipeline subobject

    std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);

    UINT currentIndex = 0;

    
    // Libraries --------------------

    m_rayGenLibrary = CompileShaderLibrary(L"RayGen.hlsl");
    m_missLibrary = CompileShaderLibrary(L"Miss.hlsl");
    m_hitLibrary = CompileShaderLibrary(L"Hit.hlsl");
    m_shadowLibrary = CompileShaderLibrary(L"ShadowRay.hlsl");


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

    std::vector<D3D12_EXPORT_DESC> hitExportDesc = {
        { L"ClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"PlaneClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE } };
    D3D12_DXIL_LIBRARY_DESC hitLibDesc = {
        {m_hitLibrary->GetBufferPointer(), m_hitLibrary->GetBufferSize()},
        2, hitExportDesc.data() };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &hitLibDesc };

    std::vector<D3D12_EXPORT_DESC> shadowExportDesc = {
        { L"ShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"ShadowMiss" , nullptr, D3D12_EXPORT_FLAG_NONE } };
    D3D12_DXIL_LIBRARY_DESC shadowLibDesc = {
        {m_shadowLibrary->GetBufferPointer(), m_shadowLibrary->GetBufferSize()},
        2, shadowExportDesc.data() };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &shadowLibDesc };

    // Hit groups --------------------

    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.HitGroupExport = L"HitGroup";
    hitGroupDesc.AnyHitShaderImport = nullptr;
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
    hitGroupDesc.IntersectionShaderImport = nullptr;

    D3D12_STATE_SUBOBJECT hitGroup = {};
    hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroup.pDesc = &hitGroupDesc;
    subobjects[currentIndex++] = hitGroup;

    D3D12_HIT_GROUP_DESC planeHitGroupDesc =
    { L"PlaneHitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, nullptr, L"PlaneClosestHit" , nullptr };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &planeHitGroupDesc };

    D3D12_HIT_GROUP_DESC shadowHitGroupDesc =
    { L"ShadowHitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, nullptr, L"ShadowClosestHit" , nullptr };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &shadowHitGroupDesc };

    // Shader config --------------------

    D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
    shaderDesc.MaxPayloadSizeInBytes = 4 * sizeof(float);
    shaderDesc.MaxAttributeSizeInBytes = 2 * sizeof(float);

    D3D12_STATE_SUBOBJECT shaderConfigObject = {};
    shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigObject.pDesc = &shaderDesc;
    subobjects[currentIndex++] = shaderConfigObject;

    // Payload association --------------------

    std::vector<std::wstring> exportedSymbols = { L"RayGen", L"Miss", L"ShadowMiss", L"HitGroup", L"PlaneHitGroup", L"ShadowHitGroup" };
    std::vector<LPCWSTR> exportedSymbolPointers = {};

    exportedSymbolPointers.reserve(exportedSymbols.size());
    for (const auto& name : exportedSymbols) {
        exportedSymbolPointers.push_back(name.c_str());
    }

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
    shaderPayloadAssociation.NumExports = static_cast<UINT>(exportedSymbols.size());
    shaderPayloadAssociation.pExports = exportedSymbolPointers.data();
    shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(currentIndex - 1)];

    D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
    shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;
    subobjects[currentIndex++] = shaderPayloadAssociationObject;

    // Root Signature --------------------

    m_rayGenSignature = CreateRayGenSignature();
    m_missSignature   = CreateMissSignature();
    m_hitSignature    = CreateHitSignature();
    m_shadowSignature = CreateHitSignature();

    ID3D12RootSignature* rayGenSignature = m_rayGenSignature.Get();
    ID3D12RootSignature* missSignature   = m_missSignature.Get();
    ID3D12RootSignature* hitSignature    = m_hitSignature.Get();
    ID3D12RootSignature* shadowSignature = m_shadowSignature.Get();

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &rayGenSignature };
    LPCWSTR rayGenSymbolPointers = L"RayGen" ;
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenAssociation = {};
    rayGenAssociation.pSubobjectToAssociate = &subobjects[currentIndex - 1];
    rayGenAssociation.NumExports = 1;
    rayGenAssociation.pExports = &rayGenSymbolPointers;
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &rayGenAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &missSignature };
    LPCWSTR missSymbolPointers = L"Miss";
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missAssociation = { &subobjects[currentIndex - 1], 1, &missSymbolPointers };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &missAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &hitSignature };
    LPCWSTR hitSymbolPointers = L"HitGroup";
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION hitAssociation = { &subobjects[currentIndex - 1], 1, &hitSymbolPointers };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &hitAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &shadowSignature };
    LPCWSTR shadowSymbolPointers = L"ShadowHitGroup";
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shadowAssociation = { &subobjects[currentIndex - 1], 1, &shadowSymbolPointers };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &shadowAssociation };

    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &missSignature };
    std::vector<LPCWSTR> missShadowSymbolPointers = { L"Miss", L"ShadowMiss" };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missShadowAssociation = { &subobjects[currentIndex - 1], 2, missShadowSymbolPointers.data() };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &missShadowAssociation };


    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &hitSignature };
    std::vector<LPCWSTR> hitPlaneSymbolPointers = { L"HitGroup", L"PlaneHitGroup"};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION planeHitAssociation = { &subobjects[currentIndex - 1], 2, hitPlaneSymbolPointers.data() };
    subobjects[currentIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &planeHitAssociation };


    // Root signature --------------------

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

    // Pipeline config --------------------

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 2;

    D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
    pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigObject.pDesc = &pipelineConfig;

    subobjects[currentIndex++] = pipelineConfigObject;

    // Create state object --------------------

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
    m_outputResource->SetName(L"Raytracing output buffer");
}

void Raytracing::CreateShaderResourceHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvUavHeap));

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc, handle);

    handle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
    m_device->CreateShaderResourceView(nullptr, &srvDesc, handle);

    handle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(ConstantBuffer);
    m_device->CreateConstantBufferView(&cbvDesc, handle);
}

void Raytracing::CreateShaderBindingTable() {
    // A SBT entry is made of a program ID and a set of parameters, taking 8 bytes each. 
    UINT m_progIdSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    m_rayGenEntrySize   = ROUND_UP(m_progIdSize + 8 * 1, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    m_missEntrySize     = ROUND_UP(m_progIdSize + 8 * 1 /*0*/, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT); // no param
    m_hitGroupEntrySize = ROUND_UP(m_progIdSize + 8 * 2, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    m_rayGenSectionSize = m_rayGenEntrySize;
    m_missSectionSize = m_missEntrySize * 2;
    m_hitGroupSectionSize = m_hitGroupEntrySize * 3;
    m_sbtSize = ROUND_UP(m_rayGenSectionSize + m_missSectionSize + m_hitGroupSectionSize, 256);

    uint8_t* pData;
    m_sbtStorage = CreateBuffer(m_sbtSize, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    m_sbtStorage->Map(0, nullptr, reinterpret_cast<void**>(&pData));

    auto heapPointer = reinterpret_cast<UINT64*>(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr);
    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"RayGen"), m_progIdSize); // Copy the shader identifier
    memcpy(pData + m_progIdSize, &heapPointer, 8 * 1); // Copy all its resources pointers or values in bulk
    pData += m_rayGenEntrySize;

    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"Miss"), m_progIdSize);
    pData += m_missEntrySize;

    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"ShadowMiss"), m_progIdSize);
    pData += m_missEntrySize;

    std::vector<void*> vertexHeapPointer = {
        (void*)(m_vertexBuffer->GetGPUVirtualAddress()),
        (void*)(m_indexBuffer->GetGPUVirtualAddress())
    };
    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"HitGroup"), m_progIdSize);
    memcpy(pData + m_progIdSize, vertexHeapPointer.data(), 8 * vertexHeapPointer.size());
    pData += m_hitGroupEntrySize;

    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"ShadowHitGroup"), m_progIdSize);
    pData += m_hitGroupEntrySize;

    memcpy(pData, m_rtStateObjectProps->GetShaderIdentifier(L"PlaneHitGroup"), m_progIdSize);
    memcpy(pData + m_progIdSize, &heapPointer, 8 * 1);


    m_sbtStorage->Unmap(0, nullptr);
}
