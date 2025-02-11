#pragma once

#include "D3dApp.h"
#include "FromBook/FrameResource.h"
#include "FromBook/UploadBuffer.h"

static const int kNumFrameResources = 3;

// Lightweight structure that stores parameters to draw a shape
struct RenderItem 
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space relative to world space,
	// which defines position, orientation, and scale
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need
	// to update the constant buffer. Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource. Thus, when we modify obect data we should set
	// NumFramesDirty = kNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = kNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item
	UINT ObjConstantBufferIndex = -1;

	MeshGeometry* Geometry = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstance parameters
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum DemoType
{
	Shapes,
	LandAndWaves
};

class MyApp : public D3DApp 
{
	public:
		MyApp(HINSTANCE hInstance);
		MyApp(const MyApp& rhs) = delete;
		MyApp& operator=(const MyApp& rhs) = delete;
		~MyApp();

		virtual bool Initialize() override;

		DemoType demo = DemoType::Shapes;

	protected:
		virtual void OnResize() override;
		virtual void Update(const GameTimer& gt) override;
		virtual void Draw(const GameTimer& gt) override;

		virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
		virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
		virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

		void UpdateObjectConstBuffers(const GameTimer& gt);
		void UpdateMainPassConstBuffers(const GameTimer& gt);

		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems);

		void BuildInputLayoutAndShaders();
		void BuildDescriptorHeaps();
		void BuildConstantBuffers();
		void BuildRootSignature();
		void BuildGeometry();
		void BuildShapesGeometry();
		void BuildRenderItems();
		void BuildFrameResources();
		void BuildPipelineStateObject();

	protected:
		std::vector<D3D12_INPUT_ELEMENT_DESC> InputLayout;

		Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineStateObject;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CbvHeap;

		std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> Geometries;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> Shaders;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> PSOs;

		// Frame resources
		std::vector<std::unique_ptr<FrameResource>> FrameResources;
		FrameResource* CurrFrameResource;
		int CurrFrameResourceIndex = 0;

		UINT PassCbvOffset = 0;
		
		// Render item lists
		std::vector<std::unique_ptr<RenderItem>> AllRenderItems;
		std::vector<RenderItem*> OpaqueRenderItems;
		std::vector<RenderItem*> TransparentRenderItems;

		PassConstants MainPassConstBuffer;

		Microsoft::WRL::ComPtr<ID3DBlob> VertexShaderByteCode = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> PixelShaderByteCode = nullptr;

		DirectX::XMFLOAT3 EyePos = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();

		float Theta = 1.5f * DirectX::XM_PI;
		float Phi = 0.2f*DirectX::XM_PI;
		float Radius = 15.0f;

		POINT LastMousePos;
};