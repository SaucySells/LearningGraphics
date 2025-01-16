#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "FromBook/d3dUtil.h"
#include "FromBook/GameTimer.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class D3DApp
{
	public:
		D3DApp(HINSTANCE hInstance);
		D3DApp(const D3DApp& rhs) = delete;
		D3DApp& operator=(const D3DApp& rhs) = delete;
		~D3DApp();

	public:
		static D3DApp* GetApp();

		HINSTANCE GetAppInstance() const;
		HWND GetMainWindow() const;
		float GetAspectRatio() const;

		bool Get4xMsaaState() const;
		void Set4xMsaaState(bool value);

		int Run();

		virtual bool Initialize();
		virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		
	protected:
		virtual void CreateRtvAndDsvDescriptorHeaps();
		virtual void OnResize();
		virtual void Update(const GameTimer& gt) {};
		virtual void Draw(const GameTimer& gt) {};


		// Convenience overrides for handling mouse input.
		virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
		virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
		virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

		bool InitMainWindow();
		bool InitDirect3D();
		void CreateCommandObjects();
		void CreateSwapChain();

		void FlushCommandQueue();

		void CalculateFrameStats();

		ID3D12Resource* CurrentBackBuffer() const;
		D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
		D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;


	protected:

		static D3DApp* App;

		HINSTANCE AppInstance = nullptr;	// application instance handle
		HWND      MainWindow = nullptr;		// main window handle
		bool      IsAppPaused = false;		// is the application paused?
		bool      IsAppMinimized = false;   // is the application minimized?
		bool      IsAppMaximized = false;   // is the application maximized?
		bool      IsResizing = false;		// are the resize bars being dragged?
		bool      FullscreenState = false;	// fullscreen enabled

		GameTimer Timer;
		
		Microsoft::WRL::ComPtr<ID3D12Device> D3dDevice;
		Microsoft::WRL::ComPtr<IDXGIFactory4> DxgiFactory;
		Microsoft::WRL::ComPtr<IDXGISwapChain> SwapChain;

		Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
		UINT64 CurrentFence = 0;

		Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> DirectCmdListAlloc;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;

		static const int SwapChainBufferCount = 2;
		int CurrBackBuffer = 0;
		Microsoft::WRL::ComPtr<ID3D12Resource> SwapChainBuffer[SwapChainBufferCount];
		Microsoft::WRL::ComPtr<ID3D12Resource> DepthStencilBuffer;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvHeap;

		D3D12_VIEWPORT ScreenViewport;
		D3D12_RECT ScissorRect;

		UINT RtvDescriptorSize = 0;
		UINT DsvDescriptorSize = 0;
		UINT CbvSrvUavDescriptorSize = 0;

		bool Msaa4xState = false;
		UINT Msaa4xQuality = 0;

		std::wstring MainWindowCaption = L"Clockwork Revolution";
		D3D_DRIVER_TYPE D3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
		DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		int ClientWidth = 800;
		int ClientHeight = 600;
};