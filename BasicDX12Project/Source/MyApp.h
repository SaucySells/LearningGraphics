#pragma once

#include "D3dApp.h"

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

};