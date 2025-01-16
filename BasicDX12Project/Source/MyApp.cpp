#include "MyApp.h"
#include <DirectXColors.h>

using namespace DirectX;

MyApp::MyApp(HINSTANCE hInstance)
: D3DApp(hInstance)
{
	ClientWidth = 1280;
	ClientHeight = 720;
}

MyApp::~MyApp()
{
}

//=========================================================================================
bool MyApp::Initialize()
{
	if (!D3DApp::Initialize())
	{
		return false;
	}

	return true;
}

//=========================================================================================
void MyApp::OnResize()
{
	D3DApp::OnResize();
}

//=========================================================================================
void MyApp::Update(const GameTimer& gt)
{

}

//=========================================================================================
void MyApp::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording
	// We can only reset when the associated command lists have finished execution on the GPU
	ThrowIfFailed(DirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList
	// Reusing the command list reuses memory
	ThrowIfFailed(CommandList->Reset(DirectCmdListAlloc.Get(), nullptr));

	// Indicate a state transition on the resource usage
	CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set the viewport and scissor rect. This needs to be reset whenever the command list is reset
	CommandList->RSSetViewports(1, &ScreenViewport);
	CommandList->RSSetScissorRects(1, &ScissorRect);

	// Clear the back buffer and depth buffer
	CommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LimeGreen, 0, nullptr);
	CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to
	CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

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

}

//=========================================================================================
void MyApp::OnMouseUp(WPARAM btnState, int x, int y)
{

}

//=========================================================================================
void MyApp::OnMouseMove(WPARAM btnState, int x, int y)
{

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