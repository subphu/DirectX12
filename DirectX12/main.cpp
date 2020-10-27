#include "stdafx.h"

void Update() {
    if (GetKeyState(VK_LBUTTON) & 0x8000) {
        POINT newCursor;
        GetCursorPos(&newCursor);

        float distance = 0.0f;
        XMStoreFloat(&distance, XMVector3Length(camPosition));

        camPosition += camFront * mouseZ * 0.02f;
        camPosition += camRight * float(newCursor.x - cursor.x) * 0.2f;
        camPosition += camUp    * float(newCursor.y - cursor.y) * 0.2f;

        float newDistance = 0.0f;
        XMStoreFloat(&newDistance, XMVector3Length(camPosition));
        camPosition = camPosition * distance / newDistance;

        camFront = XMVector3Normalize(camTarget - camPosition);
        camRight = XMVector3Normalize(XMVector3Cross(camFront, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
        camUp    = XMVector3Normalize(XMVector3Cross(camRight, camFront));

        camTarget = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
        camView = XMMatrixLookAtLH(camPosition, camTarget, camUp);
    }

    if (mouseZ != 0) {
        camPosition += camFront * mouseZ * 0.002f;
        camView = XMMatrixLookAtLH(camPosition, camTarget, camUp);
        mouseZ = 0;
    }

    GetCursorPos(&cursor);
    

    XMMATRIX model = XMMatrixIdentity();
    XMVECTOR rotAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX rotation = XMMatrixRotationAxis(rotAxis, XMConvertToRadians(angle+45));
    XMMATRIX translation = XMMatrixTranslation(-1.0f, 0.0f, 0.0f);
    XMMATRIX scale = XMMatrixScaling(0.05f, 0.05f, 0.05f);

    cbData.model = model * rotation * translation * scale;
    cbData.view = camView;
    cbData.projection = camProjection;

    UINT8* destination = constantBufferData + sizeof(ConstantBuffer) * frameIndex;
    memcpy(destination, &cbData, sizeof(ConstantBuffer));
}

void InitCamera() {
    camPosition = XMVectorSet(0.0f, 3.0f, 5.0f, 0.0f);
    camTarget = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    camUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    camFront = XMVector3Normalize(camTarget - camPosition);
    camRight = XMVector3Normalize(XMVector3Cross(camFront, camUp));
    camView = XMMatrixLookAtLH(camPosition, camTarget, camUp);
    camProjection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), float(Width) / float(Height), 1.0f, 1000.0f);

    XMFLOAT3 tempFront;
    XMStoreFloat3(&tempFront, camFront);
    yaw = XMConvertToDegrees(atan2(tempFront.z, tempFront.x));
    pitch = XMConvertToDegrees(atan2(tempFront.y, -tempFront.z));
}

void UpdatePipeline() {
    HRESULT hr;

    commandAllocator[frameIndex]->Reset();
    commandList->Reset(commandAllocator[frameIndex], pipelineStateObject);
    
    commandList->SetPipelineState(pipelineStateObject);
    commandList->SetGraphicsRootSignature(rootSignature);
    
    commandList->SetGraphicsRootConstantBufferView(GraphicsRootCBV, constantBuffer->GetGPUVirtualAddress());
    //commandList->SetGraphicsRootConstantBufferView(GraphicsRootCBV, constantBuffer->GetGPUVirtualAddress() + frameIndex * sizeof(ConstantBuffer));


    D3D12_RESOURCE_BARRIER resourceBarrierToTarget = {};
    resourceBarrierToTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToTarget.Transition.pResource = renderTargets[frameIndex];
    resourceBarrierToTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    resourceBarrierToTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    resourceBarrierToTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &resourceBarrierToTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
    size_t startPtr = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    rtvHandle.ptr = startPtr + size_t(frameIndex) * size_t(rtvDescriptorSize);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.5f, 0.5f, 0.5f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // draw triangle

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect); 
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);


    ID3D12DescriptorHeap* ppHeaps[] = { srvUavDescriptorHeap };
    commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    for (UINT i = 0; i < threadCount; i++) {
        const UINT srvIdx = i + (srvIndex[i] == 0 ? UINT(SrvParticle0) : UINT(SrvParticle1));

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += size_t(srvIdx) * size_t(srvUavDescriptorSize);
        commandList->SetGraphicsRootDescriptorTable(GraphicsRootSRVTable, srvHandle);

        commandList->DrawIndexedInstanced(36, particleCount, 0, 0, 0);
    }

    D3D12_RESOURCE_BARRIER resourceBarrierToPresent = {};
    resourceBarrierToPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToPresent.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToPresent.Transition.pResource = renderTargets[frameIndex];
    resourceBarrierToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    resourceBarrierToPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    resourceBarrierToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &resourceBarrierToPresent);

    hr = commandList->Close();
    if (FAILED(hr)) Running = false;
}

void UpdateComputePipeline(UINT threadIndex) {

    UINT srvIdx;
    UINT uavIdx;
    ID3D12Resource* pUavResource;
    if (srvIndex[threadIndex] == 0) {
        srvIdx = SrvParticle0;
        uavIdx = UavParticle1;
        pUavResource = particleBuffer1[threadIndex];
    } else {
        srvIdx = SrvParticle1;
        uavIdx = UavParticle0;
        pUavResource = particleBuffer0[threadIndex];
    }

    D3D12_RESOURCE_BARRIER resourceBarrierToUAV = {};
    resourceBarrierToUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToUAV.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToUAV.Transition.pResource = pUavResource;
    resourceBarrierToUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    resourceBarrierToUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    resourceBarrierToUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    computeCommandList[threadIndex]->ResourceBarrier(1, &resourceBarrierToUAV);

    computeCommandList[threadIndex]->SetPipelineState(computeStateObject);
    computeCommandList[threadIndex]->SetComputeRootSignature(computeRootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { srvUavDescriptorHeap };
    computeCommandList[threadIndex]->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    srvHandle.ptr += (size_t(srvIdx) + threadIndex) * size_t(srvUavDescriptorSize);

    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = srvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    uavHandle.ptr += (size_t(uavIdx) + threadIndex) * size_t(srvUavDescriptorSize);

    computeCommandList[threadIndex]->SetComputeRootConstantBufferView(ComputeRootCBV, constantBufferCS->GetGPUVirtualAddress());
    computeCommandList[threadIndex]->SetComputeRootDescriptorTable(ComputeRootSRVTable, srvHandle);
    computeCommandList[threadIndex]->SetComputeRootDescriptorTable(ComputeRootUAVTable, uavHandle);

    computeCommandList[threadIndex]->Dispatch(static_cast<int>(ceil(particleCount / 128.0f)), 1, 1);

    D3D12_RESOURCE_BARRIER resourceBarrierToResource = {};
    resourceBarrierToResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierToResource.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierToResource.Transition.pResource = pUavResource;
    resourceBarrierToResource.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    resourceBarrierToResource.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    resourceBarrierToResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    computeCommandList[threadIndex]->ResourceBarrier(1, &resourceBarrierToResource);
}

void Render() {
    WaitForPreviousFrame();

    HRESULT hr;

    UpdatePipeline();

    ID3D12CommandList* ppCommandLists[] = { commandList };
     
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
    if (FAILED(hr)) Running = false;

    hr = swapChain->Present(0, 0);
    if (FAILED(hr)) Running = false; 
}

void WaitForPreviousFrame() {
    HRESULT hr;

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    if (fence[frameIndex]->GetCompletedValue() < fenceValue[frameIndex]) {
        hr = fence[frameIndex]->SetEventOnCompletion(fenceValue[frameIndex], fenceEvent);
        if (FAILED(hr)) Running = false; 

        WaitForSingleObject(fenceEvent, INFINITE);
    }

    fenceValue[frameIndex]++;
}

void mainloop() {
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));

    while (true) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            Update();
            Render();
        }
    }
}

void Cleanup() {
    InterlockedExchange(&terminating, 1);
    WaitForMultipleObjects(threadCount, threadHandles, TRUE, INFINITE);

    for (int i = 0; i < frameBufferCount; ++i) {
        frameIndex = i;
        WaitForPreviousFrame();
    }

    for (int n = 0; n < threadCount; n++) {
        CloseHandle(threadHandles[n]);
        CloseHandle(computeFenceEvent[n]);
    }

    for (int i = 0; i < threadCount; ++i) {
        SAFE_RELEASE(particleBuffer0[i]);
        SAFE_RELEASE(particleBuffer1[i]);
    }

    SAFE_RELEASE(depthStencilBuffer);
    SAFE_RELEASE(dsDescriptorHeap);

    for (int i = 0; i < threadCount; ++i) {
        SAFE_RELEASE(computeCommandAllocator[i]);
        SAFE_RELEASE(computeCommandQueue[i]);
        SAFE_RELEASE(computeCommandList[i]);
    };

    for (int i = 0; i < frameBufferCount; ++i) {
        SAFE_RELEASE(renderTargets[i]);
        SAFE_RELEASE(commandAllocator[i]);
        SAFE_RELEASE(fence[i]);
    };

    SAFE_RELEASE(constantBuffer);
    SAFE_RELEASE(vertexBuffer);
    SAFE_RELEASE(indexBuffer);

    SAFE_RELEASE(computeStateObject);
    SAFE_RELEASE(computeRootSignature);

    SAFE_RELEASE(pipelineStateObject);
    SAFE_RELEASE(rootSignature);

    SAFE_RELEASE(commandList);
    SAFE_RELEASE(srvUavDescriptorHeap);
    SAFE_RELEASE(rtvDescriptorHeap);
    SAFE_RELEASE(swapChain);
    SAFE_RELEASE(commandQueue);
    SAFE_RELEASE(device);
}

bool InitD3D() {
    HRESULT hr;
    IDXGIFactory4* dxgiFactory;
    CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    CreateDevice(dxgiFactory);
    CreateCommandQueue();
    CreateSwapChain(dxgiFactory);
    CreateRTV();
    CreateCommandList();
    CreateFence();
    CreateGraphicsPipelineStateObj();

    CreateComputeDescriptorHeap();
    CreateComputeRootSignature();
    CreateComputePipelineStateObj();

    // create input buffer
    int vBufferSize = sizeof(vList);
    CreateBufferTransition(vBufferSize, &vertexBuffer, reinterpret_cast<BYTE*>(vList));
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vBufferSize;

    int iBufferSize = sizeof(iList);
    CreateBufferTransition(iBufferSize, &indexBuffer, reinterpret_cast<BYTE*>(iList));
    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    indexBufferView.SizeInBytes = iBufferSize;

    CreateConstantBuffer();

    CreateDepthStencilBuffer();

    CreateComputeBuffer();

    // execute the command list
    commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    fenceValue[frameIndex]++;
    hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
    if (FAILED(hr)) Running = false; 

    
    CreateComputeCommandList();

    InitViewport();

    return true;
}

void CreateComputeCommandList() {
    for (int i = 0; i < threadCount; i++) {
        D3D12_COMMAND_QUEUE_DESC cqDesc = {};
        cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cqDesc.Priority = 0;
        cqDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

        device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&computeCommandQueue[i]));
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeCommandAllocator[i]));
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, computeCommandAllocator[i], nullptr, IID_PPV_ARGS(&computeCommandList[i]));
        device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&computeFence[i]));


        computeFenceEvent[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        threadHandles[i] = CreateThread(
            nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(ComputeThread),
            reinterpret_cast<void*>(&i),
            CREATE_SUSPENDED,
            nullptr);

        if (threadHandles[i]) {
            ResumeThread(threadHandles[i]);
        }

    }
}

DWORD ComputeThread(int* threadIndex) {
    int thIdx = (*threadIndex);
    while (0 == InterlockedCompareExchange(&terminating, 0, 0)) {
        UpdateComputePipeline(thIdx);

        computeCommandList[thIdx]->Close();
        ID3D12CommandList* ppCommandLists[] = { computeCommandList[thIdx] };

        computeCommandQueue[thIdx]->ExecuteCommandLists(1, ppCommandLists);

        // Wait for the compute shader to complete the simulation.
        UINT64 threadFenceValue = InterlockedIncrement(&computeFenceValue[thIdx]);
        computeCommandQueue[thIdx]->Signal(computeFence[thIdx], threadFenceValue);
        computeFence[thIdx]->SetEventOnCompletion(threadFenceValue, computeFenceEvent[thIdx]);
        WaitForSingleObject(computeFenceEvent[thIdx], INFINITE);

        // Swap the indices to the SRV and UAV.
        srvIndex[thIdx] = 1 - srvIndex[thIdx];

        computeCommandAllocator[thIdx]->Reset();
        computeCommandList[thIdx]->Reset(computeCommandAllocator[thIdx], computeStateObject);
    }

    return 0;
}

void CreateComputeBuffer() {
    std::vector<Particle> data;
    data.resize(particleCount);
    const UINT dataSize = particleCount * sizeof(Particle);

    srand(0);
    for (UINT i = 0; i < particleCount; i++) {
        data[i].pos.x = static_cast<float>((rand() % 10000) - 5000) / 5000;
        data[i].pos.y = static_cast<float>((rand() % 10000) - 5000) / 5000;
        data[i].pos.z = static_cast<float>((rand() % 10000) - 5000) / 5000;
    }

    for (UINT i = 0; i < threadCount; i++) {
        CreateBufferTransition(dataSize, &particleBuffer0[i], reinterpret_cast<BYTE*>(data.data()),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CreateBufferTransition(dataSize, &particleBuffer1[i], reinterpret_cast<BYTE*>(data.data()),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = particleCount;
        srvDesc.Buffer.StructureByteStride = sizeof(Particle);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle0 = srvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle1 = srvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle0.ptr += (size_t(SrvParticle0) + i) * size_t(srvUavDescriptorSize);
        srvHandle1.ptr += (size_t(SrvParticle1) + i) * size_t(srvUavDescriptorSize);
        device->CreateShaderResourceView(particleBuffer0[i], &srvDesc, srvHandle0);
        device->CreateShaderResourceView(particleBuffer1[i], &srvDesc, srvHandle1);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = particleCount;
        uavDesc.Buffer.StructureByteStride = sizeof(Particle);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle0 = srvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle1 = srvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        uavHandle0.ptr += (size_t(UavParticle0) + i) * size_t(srvUavDescriptorSize);
        uavHandle1.ptr += (size_t(UavParticle1) + i) * size_t(srvUavDescriptorSize);
        device->CreateUnorderedAccessView(particleBuffer0[i], nullptr, &uavDesc, uavHandle0);
        device->CreateUnorderedAccessView(particleBuffer1[i], nullptr, &uavDesc, uavHandle1);
    }
    return;
}

void CreateComputeDescriptorHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = DescriptorCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvUavDescriptorHeap));
    srvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void CreateComputeRootSignature() {
    D3D12_DESCRIPTOR_RANGE1  descriptorTableRanges[2];
    descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorTableRanges[0].NumDescriptors = 1;
    descriptorTableRanges[0].BaseShaderRegister = 0;
    descriptorTableRanges[0].RegisterSpace = 0;
    descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    descriptorTableRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

    descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorTableRanges[1].NumDescriptors = 1;
    descriptorTableRanges[1].BaseShaderRegister = 0;
    descriptorTableRanges[1].RegisterSpace = 0;
    descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    descriptorTableRanges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;


    D3D12_ROOT_DESCRIPTOR_TABLE1 descriptorTables[2];
    descriptorTables[0].NumDescriptorRanges = 1;
    descriptorTables[0].pDescriptorRanges = &descriptorTableRanges[0];
    descriptorTables[1].NumDescriptorRanges = 1;
    descriptorTables[1].pDescriptorRanges = &descriptorTableRanges[1];

    D3D12_ROOT_PARAMETER1 rootParameters[ComputeRootParametersCount];
    D3D12_ROOT_DESCRIPTOR1 rootDesc;
    rootDesc.ShaderRegister = 0;
    rootDesc.RegisterSpace = 0;
    rootDesc.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    rootParameters[ComputeRootCBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[ComputeRootCBV].Descriptor = rootDesc;
    rootParameters[ComputeRootCBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[ComputeRootSRVTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ComputeRootSRVTable].DescriptorTable = descriptorTables[0];
    rootParameters[ComputeRootSRVTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[ComputeRootUAVTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ComputeRootUAVTable].DescriptorTable = descriptorTables[1];
    rootParameters[ComputeRootUAVTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* signature;
    D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, nullptr);

    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature));
}

HRESULT CreateComputePipelineStateObj() {
    ID3DBlob* computeShader;
    ID3DBlob* errorBuff;

    HRESULT hr = D3DCompileFromFile(L"ComputeShader.hlsl",
        nullptr, nullptr,
        "main", "cs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &computeShader, &errorBuff);

    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBuff->GetBufferPointer());
        return hr;
    }

    D3D12_SHADER_BYTECODE computeShaderBytecode = {};
    computeShaderBytecode.BytecodeLength = computeShader->GetBufferSize();
    computeShaderBytecode.pShaderBytecode = computeShader->GetBufferPointer();

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = computeRootSignature;
    computePsoDesc.CS = computeShaderBytecode;

    return device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&computeStateObject));
}

void CreateDevice(IDXGIFactory4* dxgiFactory) {

    IDXGIAdapter1* adapter;
    int adapterIndex = 0;
    bool adapterFound = false;

    while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapterIndex++;
            continue;
        }

        HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(hr)) {
            adapterFound = true;
            break;
        }

        adapterIndex++;
    }

    if (!adapterFound) return;

    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
}

void CreateCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue));
}

void CreateSwapChain(IDXGIFactory4* dxgiFactory) {
    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = Width;
    backBufferDesc.Height = Height;
    backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = frameBufferCount;
    swapChainDesc.BufferDesc = backBufferDesc;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = !FullScreen;

    IDXGISwapChain* tempSwapChain;
    dxgiFactory->CreateSwapChain(commandQueue, &swapChainDesc, &tempSwapChain);

    swapChain = static_cast<IDXGISwapChain3*>(tempSwapChain);
    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = frameBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < frameBufferCount; i++) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }
}

void CreateCommandList() {
    for (int i = 0; i < frameBufferCount; i++) {
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[i]));
    }

    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[frameIndex], NULL, IID_PPV_ARGS(&commandList));
}

void CreateFence() {
    for (int i = 0; i < frameBufferCount; i++) {
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i]));
        fenceValue[i] = 0;
    }

    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void CreateBufferTransition(int bufferSize, ID3D12Resource** dstBuffer, BYTE* data, D3D12_RESOURCE_FLAGS dstFlags, D3D12_RESOURCE_STATES dstStates) {

    // create upload universal buffer
    D3D12_RESOURCE_DESC resourceDescUpload = {};
    resourceDescUpload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDescUpload.Alignment = 0;
    resourceDescUpload.Width = bufferSize;
    resourceDescUpload.Height = 1;
    resourceDescUpload.DepthOrArraySize = 1;
    resourceDescUpload.MipLevels = 1;
    resourceDescUpload.Format = DXGI_FORMAT_UNKNOWN;
    resourceDescUpload.SampleDesc.Count = 1;
    resourceDescUpload.SampleDesc.Quality = 0;
    resourceDescUpload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDescUpload.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapPropertiesUpload = {};
    heapPropertiesUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapPropertiesUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapPropertiesUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapPropertiesUpload.CreationNodeMask = 1;
    heapPropertiesUpload.VisibleNodeMask = 1;

    ID3D12Resource* srcBuffer;
    device->CreateCommittedResource(
        &heapPropertiesUpload,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescUpload,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&srcBuffer));
    srcBuffer->SetName(L"Buffer Upload Resource Heap");

    // create default GPU buffer
    D3D12_RESOURCE_DESC resourceDescDefault = resourceDescUpload;
    resourceDescDefault.Flags = dstFlags;

    D3D12_HEAP_PROPERTIES heapPropertiesDefault = {};
    heapPropertiesDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapPropertiesDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapPropertiesDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapPropertiesDefault.CreationNodeMask = 1;
    heapPropertiesDefault.VisibleNodeMask = 1;

    device->CreateCommittedResource(
        &heapPropertiesDefault,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescDefault,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(dstBuffer));
    (*dstBuffer)->SetName(L"Buffer Default Resource Heap");

    // copy the data from the upload heap to the default heap
    BYTE* pData;
    srcBuffer->Map(0, NULL, reinterpret_cast<void**>(&pData));
    
    memcpy(pData, data, bufferSize);

    srcBuffer->Unmap(0, NULL);

    commandList->CopyBufferRegion(*dstBuffer, 0, srcBuffer, 0, bufferSize);

    D3D12_RESOURCE_BARRIER resourceBarrier = {};
    resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrier.Transition.pResource = *dstBuffer;
    resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    resourceBarrier.Transition.StateAfter = dstStates;
    resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &resourceBarrier);

}

void CreateDepthStencilBuffer() {

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsDescriptorHeap));

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = Width;
    resourceDesc.Height = Height;
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

    device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&depthStencilBuffer)
    );

    dsDescriptorHeap->SetName(L"Depth/Stencil Resource Heap");

    device->CreateDepthStencilView(depthStencilBuffer, &depthStencilDesc, dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


}

HRESULT CreateGraphicsPipelineStateObj() {
    ID3DBlob* vertexShader;
    ID3DBlob* errorBuff;

    HRESULT hr = D3DCompileFromFile(L"VertexShader.hlsl",
        nullptr, nullptr,
        "main", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &vertexShader, &errorBuff);

    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBuff->GetBufferPointer());
        return hr;
    }

    D3D12_SHADER_BYTECODE vertexShaderBytecode = {};
    vertexShaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
    vertexShaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();

    ID3DBlob* pixelShader;
    hr = D3DCompileFromFile(L"PixelShader.hlsl",
        nullptr, nullptr,
        "main", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &pixelShader, &errorBuff);

    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBuff->GetBufferPointer());
        return hr;
    }

    D3D12_SHADER_BYTECODE pixelShaderBytecode = {};
    pixelShaderBytecode.BytecodeLength = pixelShader->GetBufferSize();
    pixelShaderBytecode.pShaderBytecode = pixelShader->GetBufferPointer();


    // create root signature

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
    rootParameters[GraphicsRootSRVTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[GraphicsRootSRVTable].DescriptorTable = descriptorTable;
    rootParameters[GraphicsRootSRVTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signature;
    D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, nullptr);

    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

    // create pso
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    inputLayoutDesc.pInputElementDescs = inputLayout;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = FALSE;
    renderTargetBlendDesc.LogicOpEnable = FALSE;
    renderTargetBlendDesc.SrcBlend = D3D12_BLEND_ONE;
    renderTargetBlendDesc.DestBlend = D3D12_BLEND_ZERO;
    renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
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
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = vertexShaderBytecode;
    psoDesc.PS = pixelShaderBytecode;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = 0xffffffff;
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.NumRenderTargets = 1;

    return device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject));

}

void InitViewport() {
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = float(Width);
    viewport.Height = float(Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = Width;
    scissorRect.bottom = Height;
}

void CreateConstantBuffer() {


    const UINT bufferSize = sizeof(ConstantBuffer);
    CreateBufferTransition(bufferSize, &constantBufferCS, reinterpret_cast<BYTE*>(&cbData));


    const UINT constantBufferSize = sizeof(ConstantBuffer) * frameBufferCount;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = constantBufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer));

    constantBuffer->SetName(L"Constant Buffer Upload Resource Heap");

    D3D12_RANGE readRange = { 0, 0 };
    constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferData));
    ZeroMemory(constantBufferData, constantBufferSize);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {


    if (!InitWindow(hInstance, nShowCmd, Width, Height, FullScreen)) {
        MessageBox(0, L"Window Initialization - Failed", L"Error", MB_OK);
        return 0;
    }

    if (!InitD3D()) {
        MessageBox(0, L"Failed to initialize direct3d 12", L"Error", MB_OK);
        Cleanup();
        return 1;
    }

    InitCamera();
    mainloop();
    WaitForPreviousFrame();
    CloseHandle(fenceEvent);
    Cleanup();
    return 0;
}

bool InitWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool fullscreen) {
    WNDCLASSEX wc;

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WindowName;

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Error registering class", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hwnd = CreateWindowEx(NULL,
        WindowName,
        WindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd) {
        MessageBox(NULL, L"Error creating window", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    ShowWindow(hwnd, ShowWnd);
    UpdateWindow(hwnd);

    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_KEYUP:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_MOUSEWHEEL:
        mouseZ += GET_WHEEL_DELTA_WPARAM(wParam);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}