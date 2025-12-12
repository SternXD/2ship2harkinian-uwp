#pragma once

#include <dxgi1_3.h>
#include <d3d11.h>

namespace dx11glue
{
	extern ID3D11Device* g_pd3dDevice;
	extern ID3D11DeviceContext* g_pd3dDeviceContext;
	extern IDXGISwapChain1* g_pSwapChain;
	extern ID3D11RenderTargetView* g_mainRenderTargetView;

	bool CreateDeviceD3D(void* hWnd, int w, int h);
	void CleanupDeviceD3D();
	void CreateRenderTarget();
	void CleanupRenderTarget();
}