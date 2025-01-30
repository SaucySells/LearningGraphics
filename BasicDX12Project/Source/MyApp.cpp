#include "MyApp.h"
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
	for (int i = 0; i < Geometry.size(); ++i)
	{
		delete Geometry.at(i);
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

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildInputLayoutAndShaders();
	BuildGeometry();
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
	// Convert spherical to cartesian coordinates
	float x = Radius * sinf(Phi) * cosf(Theta);
	float z = Radius * sinf(Phi) * sinf(Theta);
	float y = Radius * cosf(Phi);

	// Build the view matrix
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&View, view);

	XMMATRIX world = XMLoadFloat4x4(&World);
	XMMATRIX proj = XMLoadFloat4x4(&Proj);
	XMMATRIX worldViewProj = world*view*proj;

	// Upload the constant buffer with the latest WorldviewProj matrix
	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	objConstants.Time = XMFLOAT4(gt.TotalTime(), 0.0f, 0.0f, 0.0f);
	ConstBufferUpload->CopyData(0, objConstants);
}

//=========================================================================================
void MyApp::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording
	// We can only reset when the associated command lists have finished execution on the GPU
	ThrowIfFailed(DirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList
	// Reusing the command list reuses memory
	ThrowIfFailed(CommandList->Reset(DirectCmdListAlloc.Get(), PipelineStateObject.Get()));

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

	CommandList->SetGraphicsRootSignature(RootSignature.Get());
	
	CommandList->IASetVertexBuffers(0, 1, &Geometry.at(0)->VertexBufferView());
	CommandList->IASetIndexBuffer(&Geometry.back()->IndexBufferView());
	CommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CommandList->SetGraphicsRootDescriptorTable(0, CbvHeap->GetGPUDescriptorHandleForHeapStart());

	CommandList->DrawIndexedInstanced(Geometry.back()->DrawArgs["box"].IndexCount, 1, 0, 0, 0);
	CommandList->DrawIndexedInstanced(Geometry.back()->DrawArgs["triangle"].IndexCount, 1, Geometry.back()->DrawArgs["triangle"].StartIndexLocation, 0, 0);

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

	// Wait until frame commands are complete. This is inneficient, can be improved
	FlushCommandQueue();
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
		float dx = 0.005f * static_cast<float>(x - LastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - LastMousePos.y);

		// Update the camera radius based on input
		Radius += (dx - dy);

		// Restrict the radius
		Radius = MathHelper::Clamp(Radius, 3.0f, 15.0f);
	}

	LastMousePos.x = x;
	LastMousePos.y = y;
}

//=========================================================================================
void MyApp::BuildInputLayoutAndShaders()
{
	InputLayout =
	{ 
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	VertexShaderByteCode = d3dUtil::CompileShader(L"F:/DirectX12Stuff/LearningGraphics/BasicDX12Project/Source/Shaders/color.hlsl", nullptr, "VS", "vs_5_0");
	PixelShaderByteCode = d3dUtil::CompileShader(L"F:/DirectX12Stuff/LearningGraphics/BasicDX12Project/Source/Shaders/color.hlsl", nullptr, "PS", "ps_5_0");

	// Load from pre-compiled shaders
	// VertexShaderBytecode = d3dUtil::LoadBinary(L"Shaders/color_vs.cso");
	// PixelShaderBytecode = d3dUtil::LoadBinary(L"Shaders/color_ps.cso");
}

//=========================================================================================
void MyApp::BuildConstantBuffers()
{
	ConstBufferUpload = std::make_unique<UploadBuffer<ObjectConstants>>(D3dDevice.Get(), 1, true);

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// Address to start of the buffer (0th constant buffer)
	D3D12_GPU_VIRTUAL_ADDRESS constBufferAddress = ConstBufferUpload->Resource()->GetGPUVirtualAddress();

	// Offset to the ith object constant buffer in the buffer
	int boxConstBufferIndex = 0;
	constBufferAddress += boxConstBufferIndex*objCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = constBufferAddress;
	cbvDesc.SizeInBytes = objCBByteSize;

	D3dDevice->CreateConstantBufferView(&cbvDesc, CbvHeap->GetCPUDescriptorHandleForHeapStart());
}

//=========================================================================================
void MyApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;

	ThrowIfFailed(D3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&CbvHeap)));
}

//=========================================================================================
void MyApp::BuildRootSignature()
{	
	// Root parameter can be a table, root descriptor or root constants
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	// Create a single descriptor table of CBVs
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	// A root signature is an array of root parameters
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSignature.GetAddressOf(), errorBlob.GetAddressOf());

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

	Geometry.push_back(newGeometry);

}

//=========================================================================================
void MyApp::BuildPipelineStateObject()
{
	D3D12_RASTERIZER_DESC rasterDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

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