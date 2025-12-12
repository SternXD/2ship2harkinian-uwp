// File came from: https://github.com/worleydl/vpinball-dev/blob/uwp-dev/uwp/src/dx11glue.cpp

#include "dx11glue.h"
#include "libuwp.h"

namespace dx11glue
{
	// DX11 data
	ID3D11Device* g_pd3dDevice = nullptr;
	ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
	IDXGISwapChain1* g_pSwapChain = nullptr;
	ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

	bool CreateDeviceD3D(void* hwnd, int w, int h)
	{
		// Setup swap chain
		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 2;
		sd.Width = w;
		sd.Height = h;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		IDXGIFactory* pFactory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)&pFactory)))
			return false;

		IDXGIAdapter* pAdapter = nullptr;
		if (FAILED(pFactory->EnumAdapters(0, &pAdapter)))
		{
			pFactory->Release();
			return false;
		}

		UINT createDeviceFlags = 0;
		//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		D3D_FEATURE_LEVEL featureLevel;
		D3D11CreateDevice(
			pAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
		);

		IDXGIDevice* dxgiDevice = nullptr;
		g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

		IDXGIAdapter* dxgiAdapter = nullptr;
		dxgiDevice->GetAdapter(&dxgiAdapter);

		IDXGIFactory2* dxgiFactory = nullptr;
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

		dxgiFactory->CreateSwapChainForCoreWindow(
			dxgiDevice,
			static_cast<IUnknown*>(uwp_GetWindowReference()),
			&sd, nullptr, &g_pSwapChain);

		CreateRenderTarget();

		dxgiFactory->Release();
		dxgiAdapter->Release();
		dxgiDevice->Release();
		pAdapter->Release();
		pFactory->Release();

		return true;
	}

	void CleanupDeviceD3D()
	{
		CleanupRenderTarget();
		if (g_pSwapChain)
		{
			g_pSwapChain->Release();
			g_pSwapChain = nullptr;
		}
		if (g_pd3dDeviceContext)
		{
			g_pd3dDeviceContext->Release();
			g_pd3dDeviceContext = nullptr;
		}
		if (g_pd3dDevice)
		{
			g_pd3dDevice->Release();
			g_pd3dDevice = nullptr;
		}
	}

	void CreateRenderTarget()
	{
		ID3D11Texture2D* pBackBuffer;
		g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
		pBackBuffer->Release();
	}

	void CleanupRenderTarget()
	{
		if (g_mainRenderTargetView)
		{
			g_mainRenderTargetView->Release();
			g_mainRenderTargetView = nullptr;
		}
	}
}