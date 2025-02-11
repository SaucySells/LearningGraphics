#include "MyApp.h"
#include "FromBook/GeometryGenerator.h"
#include <DirectXColors.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

MyApp::MyApp(HINSTANCE hInstance)
: D3DApp(hInstance)
{
	ClientWidth = 1280;
	ClientHeight = 720;
}

MyApp::~MyApp()
{
	if (D3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

//=========================================================================================
bool MyApp::Initialize()
{
	if (!D3DApp::Initialize())
	{
		return false;
	}

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(CommandList->Reset(DirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildInputLayoutAndShaders();
	BuildShapesGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildPipelineStateObject();

	// Execute the initialization commands.
	ThrowIfFailed(CommandList->Close());
	ID3D12CommandList* cmdsLists[] = { CommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();


	return true;
}

//=========================================================================================
void MyApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, GetAspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&Proj, P);
}

//=========================================================================================
void MyApp::Update(const GameTimer& gt)
{
	// Frame Resource Update:
	// Cycle through the circular frame resource array
	CurrFrameResourceIndex = (CurrFrameResourceIndex+1) % kNumFrameResources;
	CurrFrameResource = FrameResources[CurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point
	if (CurrFrameResource->Fence != 0 && Fence->GetCompletedValue() < CurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(Fence->SetEventOnCompletion(CurrFrameResource->Fence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	// Camera Update:
	// Convert spherical to cartesian coordinates
	EyePos.x = Radius * sinf(Phi) * cosf(Theta);
	EyePos.z = Radius * sinf(Phi) * sinf(Theta);
	EyePos.y = Radius * cosf(Phi);

	// Build the view matrix
	XMVECTOR pos = XMVectorSet(EyePos.x, EyePos.y, EyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&View, view);

	// Upload the constant buffer with the latest WorldviewProj matrix
	UpdateObjectConstBuffers(gt);
	UpdateMainPassConstBuffers(gt);
}

//=========================================================================================
void MyApp::UpdateObjectConstBuffers(const GameTimer& gt)
{
	UploadBuffer<ObjectConstants>* currObjectConstBuffer = CurrFrameResource->ObjectCB.get();
	for (auto& renderItem : AllRenderItems)
	{
		// Only update the cbuffer data if the constants have changed
		// This needs to be tracked per frame resource
		if (renderItem->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&renderItem->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectConstBuffer->CopyData(renderItem->ObjConstantBufferIndex, objConstants);
			
			// Next FrameResource needs to be updated too
			renderItem->NumFramesDirty--;
		}
	}
}

//=========================================================================================
void MyApp::UpdateMainPassConstBuffers(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&View);
	XMMATRIX proj = XMLoadFloat4x4(&Proj);

	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&MainPassConstBuffer.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&MainPassConstBuffer.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&MainPassConstBuffer.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&MainPassConstBuffer.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&MainPassConstBuffer.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&MainPassConstBuffer.InvViewProj, XMMatrixTranspose(invViewProj));

	MainPassConstBuffer.EyePosW = EyePos;
	MainPassConstBuffer.RenderTargetSize = { (float)ClientWidth, (float)ClientHeight };
	MainPassConstBuffer.InvRenderTargetSize = { (1.0f/ClientWidth), (1.0f/ClientHeight) };
	MainPassConstBuffer.NearZ = 1.0f;
	MainPassConstBuffer.FarZ = 1000.0f;
	MainPassConstBuffer.DeltaTime = gt.DeltaTime();
	MainPassConstBuffer.TotalTime = gt.TotalTime();

	UploadBuffer<PassConstants>* currPassCB = CurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, MainPassConstBuffer);
}


//=========================================================================================
void MyApp::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording
	// We can only reset when the associated command lists have finished execution on the GPU
	ThrowIfFailed(CurrFrameResource->CmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList
	// Reusing the command list reuses memory
	ThrowIfFailed(CommandList->Reset(CurrFrameResource->CmdListAlloc.Get(), PipelineStateObject.Get()));

	// Indicate a state transition on the resource usage
	CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set the viewport and scissor rect. This needs to be reset whenever the command list is reset
	CommandList->RSSetViewports(1, &ScreenViewport);
	CommandList->RSSetScissorRects(1, &ScissorRect);

	// Clear the back buffer and depth buffer
	CommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to
	CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { CbvHeap.Get() };
	CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	int mainPassCbvIndex = PassCbvOffset + CurrFrameResourceIndex;
	auto mainPassCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(CbvHeap->GetGPUDescriptorHandleForHeapStart());
	mainPassCbvHandle.Offset(mainPassCbvIndex, CbvSrvUavDescriptorSize);

	CommandList->SetGraphicsRootSignature(RootSignature.Get());

	CommandList->SetGraphicsRootDescriptorTable(1, mainPassCbvHandle);
	
	DrawRenderItems(CommandList.Get(), OpaqueRenderItems);
	
	// Indicate a state transition on the resource usage
	CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands
	ThrowIfFailed(CommandList->Close());

	// Add the command list to the queue for execution
	ID3D12CommandList* cmdsList[] = { CommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(cmdsList), cmdsList);

	// Swap the back and front buffers
	ThrowIfFailed(SwapChain->Present(0, 0));
	CurrBackBuffer = (CurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point
	CurrFrameResource->Fence = ++CurrentFence;

	// Add an instruction to the command queue to set a new fence point
	// Because we are on the GPU timeline, the new fence point won't be
	// set until the GPU finishes processing all the commands prior to this Signal()
	CommandQueue->Signal(Fence.Get(), CurrentFence);
}

//=========================================================================================
void MyApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	ID3D12Resource* objectCB = CurrFrameResource->ObjectCB->Resource();

	for (size_t i = 0; i < renderItems.size(); ++i)
	{
		RenderItem* renderItem = renderItems[i];
		cmdList->IASetVertexBuffers(0, 1, &renderItem->Geometry->VertexBufferView());
		cmdList->IASetIndexBuffer(&renderItem->Geometry->IndexBufferView());
		cmdList->IASetPrimitiveTopology(renderItem->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this render item and for this frame resource
		UINT cbvIndex = CurrFrameResourceIndex * (UINT)OpaqueRenderItems.size() + renderItem->ObjConstantBufferIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(CbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, CbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(renderItem->IndexCount, 1, renderItem->StartIndexLocation, renderItem->BaseVertexLocation, 0);
	}
}

//=========================================================================================
void MyApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	LastMousePos.x = x;
	LastMousePos.y = y;

	SetCapture(MainWindow);
}

//=========================================================================================
void MyApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

//=========================================================================================
void MyApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0) // Move camera around object
	{
		// Make each pixel correspond to a quarter of a degree
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - LastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - LastMousePos.y));
		
		// Update angles based on input to orbit camera around box
		Theta += dx;
		Phi += dy;
		
		// Clamp the angle Phi
		Phi = MathHelper::Clamp(Phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0) // Move camera closer or further from object
	{
		// Make each pixel correspond to .005 units in the scene
		float dx = 0.05f * static_cast<float>(x - LastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - LastMousePos.y);

		// Update the camera radius based on input
		Radius += (dx - dy);

		// Restrict the radius
		Radius = MathHelper::Clamp(Radius, 5.0f, 150.0f);
	}

	LastMousePos.x = x;
	LastMousePos.y = y;
}

//=========================================================================================
void MyApp::BuildFrameResources()
{
	for (int i = 0; i < kNumFrameResources; ++i)
	{
		FrameResources.push_back(std::make_unique<FrameResource>(D3dDevice.Get(), 1, UINT(AllRenderItems.size()), 1));
	}
}

//=========================================================================================
void MyApp::BuildInputLayoutAndShaders()
{
	InputLayout =
	{ 
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	VertexShaderByteCode = d3dUtil::CompileShader(L"F:/DirectX12Stuff/LearningGraphics/BasicDX12Project/Source/Shaders/color.hlsl", nullptr, "VS", "vs_5_1");
	PixelShaderByteCode = d3dUtil::CompileShader(L"F:/DirectX12Stuff/LearningGraphics/BasicDX12Project/Source/Shaders/color.hlsl", nullptr, "PS", "ps_5_1");

	// Load from pre-compiled shaders
	// VertexShaderBytecode = d3dUtil::LoadBinary(L"Shaders/color_vs.cso");
	// PixelShaderBytecode = d3dUtil::LoadBinary(L"Shaders/color_ps.cso");
}

//=========================================================================================
void MyApp::BuildConstantBuffers()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)OpaqueRenderItems.size();

	// Need a CBV descriptor for each object for each frame resouce
	for (int frameIndex = 0; frameIndex < kNumFrameResources; ++frameIndex)
	{
		ID3D12Resource* objectCB = FrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the i'th object constant buffer in the current buffer
			cbAddress += i * objCBByteSize;

			// Offset to the CBV in the descriptor heap
			int heapIndex = frameIndex*objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(CbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, CbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			D3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT mainPassCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the main pass CBVs for each frame resource
	for (int frameIndex = 0; frameIndex < kNumFrameResources; ++frameIndex)
	{
		ID3D12Resource* mainPassCB = FrameResources[frameIndex]->PassCB->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mainPassCB->GetGPUVirtualAddress();

		// Offset to the main pass CBV in the descriptor heap
		int heapIndex = PassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(CbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, CbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = mainPassCBByteSize;

		D3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

//=========================================================================================
void MyApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)OpaqueRenderItems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource
	UINT numDescriptors = (objCount+1) * kNumFrameResources;

	// Save an offset to the start of the main pass CBVS. These are the last 3 descriptors
	PassCbvOffset = objCount * kNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;

	ThrowIfFailed(D3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&CbvHeap)));
}

//=========================================================================================
void MyApp::BuildRootSignature()
{	
	// Root parameter can be a table, root descriptor or root constants
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create two descriptor tables of CBVs, one for per object CBs and one for main pass CBs
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSignature.GetAddressOf(), errorBlob.GetAddressOf()));

	ThrowIfFailed(D3dDevice->CreateRootSignature(0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS(&RootSignature)));
}

//=========================================================================================
void MyApp::BuildGeometry()
{
	std::array<Vertex, 11> vertices =
	{
		Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
		Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta)}),
		Vertex({ XMFLOAT3(+2.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+4.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+3.0f, 0.0f, +1.0f), XMFLOAT4(Colors::Black) })
	};

	std::array<std::uint16_t, 42> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7,

		// extra triangle
		8, 9, 10,
		10, 9, 8

	};

	const UINT64 vertexByteSize = vertices.size() * sizeof(Vertex);
	const UINT64 indexByteSize = indices.size() * sizeof(std::uint16_t);

	MeshGeometry* newGeometry = new MeshGeometry();
	newGeometry->Name = "box";

	ThrowIfFailed(D3DCreateBlob(vertexByteSize, &newGeometry->VertexBufferCPU));
	CopyMemory(newGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexByteSize);

	ThrowIfFailed(D3DCreateBlob(indexByteSize, &newGeometry->IndexBufferCPU));
	CopyMemory(newGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexByteSize);

	newGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(D3dDevice.Get(), CommandList.Get(), vertices.data(), vertexByteSize, newGeometry->VertexBufferUploader);
	newGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(D3dDevice.Get(), CommandList.Get(), indices.data(), indexByteSize, newGeometry->IndexBufferUploader);

	newGeometry->VertexByteStride = sizeof(Vertex);
	newGeometry->VertexBufferByteSize = vertexByteSize;
	newGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	newGeometry->IndexBufferByteSize = indexByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	newGeometry->DrawArgs["box"] = submesh;

	SubmeshGeometry trisubmesh;
	trisubmesh.IndexCount = 6;
	trisubmesh.StartIndexLocation = 36;
	trisubmesh.BaseVertexLocation = 0;

	newGeometry->DrawArgs["triangle"] = trisubmesh;

	//Geometry.push_back(newGeometry);

}

//=========================================================================================
void MyApp::BuildShapesGeometry()
{
	GeometryGenerator geoGenerator;
	GeometryGenerator::MeshData box = geoGenerator.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGenerator.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGenerator.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGenerator.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	// We are concatenating all the geometry into one big vertex/index buffer,
	// so define the regions in the buffer each submesh covers

	// Cache the vertex offsets to each object in the concatenated vertex buffer
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Do the same for the index buffer
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// Define the SubmeshGeometry that cover different regions of the vertex/index buffers
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	// Extract the vertex elements we are interested in and pack the vertices
	// of all meshes into one vertex buffer
	auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}

	// Do the same for indices
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vertexBufferByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT indexBufferByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	std::unique_ptr<MeshGeometry> geometry = std::make_unique<MeshGeometry>();
	geometry->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, &geometry->VertexBufferCPU));
	CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCPU));
	CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);

	geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(D3dDevice.Get(), CommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
	geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(D3dDevice.Get(), CommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);

	geometry->VertexByteStride = sizeof(Vertex);
	geometry->VertexBufferByteSize = vertexBufferByteSize;
	geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->IndexBufferByteSize = indexBufferByteSize;

	geometry->DrawArgs["box"] = boxSubmesh;
	geometry->DrawArgs["grid"] = gridSubmesh;
	geometry->DrawArgs["sphere"] = sphereSubmesh;
	geometry->DrawArgs["cylinder"] = cylinderSubmesh;

	Geometries[geometry->Name] = std::move(geometry);
}

//=========================================================================================
void MyApp::BuildRenderItems()
{
	// Construct the scene for the "Shapes" demo
	std::unique_ptr<RenderItem> boxRenderItem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRenderItem->World, XMMatrixMultiply(XMMatrixScaling(2.0f, 2.0f, 2.0f), XMMatrixTranslation(0.0f, 0.5f, 0.0f)));
	boxRenderItem->ObjConstantBufferIndex = 0;
	boxRenderItem->Geometry = Geometries["shapeGeo"].get();
	boxRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRenderItem->IndexCount = boxRenderItem->Geometry->DrawArgs["box"].IndexCount;
	boxRenderItem->StartIndexLocation = boxRenderItem->Geometry->DrawArgs["box"].StartIndexLocation;
	boxRenderItem->BaseVertexLocation = boxRenderItem->Geometry->DrawArgs["box"].BaseVertexLocation;

	// Add to render items list
	AllRenderItems.push_back(std::move(boxRenderItem));

	std::unique_ptr<RenderItem> gridRenderItem = std::make_unique<RenderItem>();
	gridRenderItem->World = MathHelper::Identity4x4();
	gridRenderItem->ObjConstantBufferIndex = 1;
	gridRenderItem->Geometry = Geometries["shapeGeo"].get();
	gridRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRenderItem->IndexCount = gridRenderItem->Geometry->DrawArgs["grid"].IndexCount;
	gridRenderItem->StartIndexLocation = gridRenderItem->Geometry->DrawArgs["grid"].StartIndexLocation;
	gridRenderItem->BaseVertexLocation = gridRenderItem->Geometry->DrawArgs["grid"].BaseVertexLocation;

	// Add to render items list
	AllRenderItems.push_back(std::move(gridRenderItem));

	// Build the columns and spheres in rows
	UINT objCBIndex = 2;
	for (int i = 0; i < 5; ++i)
	{
		std::unique_ptr<RenderItem> leftCylinderRenderItem = std::make_unique<RenderItem>();
		std::unique_ptr<RenderItem> leftSphereRenderItem = std::make_unique<RenderItem>();
		std::unique_ptr<RenderItem> rightCylinderRenderItem = std::make_unique<RenderItem>();
		std::unique_ptr<RenderItem> rightSphereRenderItem = std::make_unique<RenderItem>();

		XMMATRIX leftCylinderWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);

		XMMATRIX rightCylinderWorld = XMMatrixTranslation(5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylinderRenderItem->World, leftCylinderWorld);
		leftCylinderRenderItem->ObjConstantBufferIndex = objCBIndex++;
		leftCylinderRenderItem->Geometry = Geometries["shapeGeo"].get();
		leftCylinderRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylinderRenderItem->IndexCount = leftCylinderRenderItem->Geometry->DrawArgs["cylinder"].IndexCount;
		leftCylinderRenderItem->StartIndexLocation = leftCylinderRenderItem->Geometry->DrawArgs["cylinder"].StartIndexLocation;
		leftCylinderRenderItem->BaseVertexLocation = leftCylinderRenderItem->Geometry->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylinderRenderItem->World, rightCylinderWorld);
		rightCylinderRenderItem->ObjConstantBufferIndex = objCBIndex++;
		rightCylinderRenderItem->Geometry = Geometries["shapeGeo"].get();
		rightCylinderRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylinderRenderItem->IndexCount = rightCylinderRenderItem->Geometry->DrawArgs["cylinder"].IndexCount;
		rightCylinderRenderItem->StartIndexLocation = rightCylinderRenderItem->Geometry->DrawArgs["cylinder"].StartIndexLocation;
		rightCylinderRenderItem->BaseVertexLocation = rightCylinderRenderItem->Geometry->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRenderItem->World, leftSphereWorld);
		leftSphereRenderItem->ObjConstantBufferIndex = objCBIndex++;
		leftSphereRenderItem->Geometry = Geometries["shapeGeo"].get();
		leftSphereRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRenderItem->IndexCount = leftSphereRenderItem->Geometry->DrawArgs["sphere"].IndexCount;
		leftSphereRenderItem->StartIndexLocation = leftSphereRenderItem->Geometry->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRenderItem->BaseVertexLocation = leftSphereRenderItem->Geometry->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRenderItem->World, rightSphereWorld);
		rightSphereRenderItem->ObjConstantBufferIndex = objCBIndex++;
		rightSphereRenderItem->Geometry = Geometries["shapeGeo"].get();
		rightSphereRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRenderItem->IndexCount = rightSphereRenderItem->Geometry->DrawArgs["sphere"].IndexCount;
		rightSphereRenderItem->StartIndexLocation = rightSphereRenderItem->Geometry->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRenderItem->BaseVertexLocation = rightSphereRenderItem->Geometry->DrawArgs["sphere"].BaseVertexLocation;

		AllRenderItems.push_back(std::move(leftCylinderRenderItem));
		AllRenderItems.push_back(std::move(rightCylinderRenderItem));
		AllRenderItems.push_back(std::move(leftSphereRenderItem));
		AllRenderItems.push_back(std::move(rightSphereRenderItem));
	}

	// All render items are opaque in the "shapes" scene
	for (auto& renderItem : AllRenderItems)
	{
		OpaqueRenderItems.push_back(renderItem.get());
	}
}

//=========================================================================================
void MyApp::BuildPipelineStateObject()
{
	D3D12_RASTERIZER_DESC rasterDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc;
	ZeroMemory(&pipelineDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	pipelineDesc.InputLayout = { InputLayout.data(), UINT(InputLayout.size()) };
	pipelineDesc.pRootSignature = RootSignature.Get();
	pipelineDesc.VS = { reinterpret_cast<BYTE*>(VertexShaderByteCode->GetBufferPointer()), VertexShaderByteCode->GetBufferSize() };
	pipelineDesc.PS = { reinterpret_cast<BYTE*>(PixelShaderByteCode->GetBufferPointer()), PixelShaderByteCode->GetBufferSize() };
	pipelineDesc.RasterizerState = rasterDesc;
	pipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pipelineDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pipelineDesc.SampleMask = UINT_MAX;
	pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = BackBufferFormat;
	pipelineDesc.SampleDesc.Count = Msaa4xState ? 4 : 1;
	pipelineDesc.SampleDesc.Quality = Msaa4xState ? (Msaa4xQuality - 1) : 0;
	pipelineDesc.DSVFormat = DepthStencilFormat;
	
	ThrowIfFailed(D3dDevice->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&PipelineStateObject)));
}

//=========================================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{	
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		MyApp myApp(hInstance);
		myApp.demo = DemoType::Shapes;
		//myApp.demo = DemoType::LandAndWaves;
		if (!myApp.Initialize())
		{
			return 0;
		}

		return myApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(0, LPCWSTR(e.ToString().c_str()), 0, 0);
		return 0;
	}

	return 0;
}