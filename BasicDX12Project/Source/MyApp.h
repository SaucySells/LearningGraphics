#pragma once

#include "D3dApp.h"
#include "FromBook/UploadBuffer.h"

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4 Time;
};

class MyApp : public D3DApp 
{
	public:
		MyApp(HINSTANCE hInstance);
		MyApp(const MyApp& rhs) = delete;
		MyApp& operator=(const MyApp& rhs) = delete;
		~MyApp();

		virtual bool Initialize() override;

	protected:
		virtual void OnResize() override;
		virtual void Update(const GameTimer& gt) override;
		virtual void Draw(const GameTimer& gt) override;

		virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
		virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
		virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

		void BuildInputLayoutAndShaders();
		void BuildDescriptorHeaps();
		void BuildConstantBuffers();
		void BuildRootSignature();
		void BuildGeometry();
		void BuildPipelineStateObject();

	protected:
		std::vector<D3D12_INPUT_ELEMENT_DESC> InputLayout;

		Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineStateObject;

		std::unique_ptr<UploadBuffer<ObjectConstants>> ConstBufferUpload;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CbvHeap;

		std::vector<MeshGeometry*> Geometry;

		Microsoft::WRL::ComPtr<ID3DBlob> VertexShaderByteCode = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> PixelShaderByteCode = nullptr;

		DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();

		float Theta = 1.5f * DirectX::XM_PI;
		float Phi = DirectX::XM_PIDIV4;
		float Radius = 5.0f;

		POINT LastMousePos;

};