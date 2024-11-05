#pragma once

#include <d3d11_4.h>
#include <dxgi1_2.h>

#define MUTEX_KEY 5556

class FrameGenerator
{
	struct VertexType
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT2 Tex;
	};

	UINT _width;
	UINT _height;

	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager;
	wil::com_ptr_nothrow<ID3D11Device1> _dxDevice;
	wil::com_ptr_nothrow<ID3D11DeviceContext> _dxDeviceContext;
	wil::com_ptr_nothrow<ID3D11Multithread> _dxDeviceMutex;
	wil::com_ptr_nothrow<ID3D11Texture2D> _renderTexture;
	wil::com_ptr_nothrow<IMFTransform> _converter;
	
	wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTargetView;
	wil::com_ptr_nothrow<ID3D11ShaderResourceView> _shaderResourceView;
	wil::com_ptr_nothrow<ID3D11VertexShader> _spriteVS;
	wil::com_ptr_nothrow<ID3D11PixelShader> _spritePS;
	wil::com_ptr_nothrow<ID3D11InputLayout> _spriteInputLayout;
	D3D11_BUFFER_DESC _vbDesc;

	wil::com_ptr_nothrow<ID3D11Texture2D> _sharedCaptureWindowTexture;
	HANDLE _sharedCaptureWindowHandle;
	int _captureTextureWidth;
	int _captureTextureHeight;
	
	wil::com_ptr_nothrow<ID3D11Texture2D> _sharedParamsTexture;
	HANDLE _sharedParamsHandle;
	wil::com_ptr_nothrow<ID3D11Texture2D> _cpuParamsTexture;
	int _captureWindowWidth;
	int _captureWindowHeight;

	HRESULT SetupD3D11Device();

	HRESULT CreateSharedCaptureWindowTexture();

	HRESULT CreateSharedParamsTexture();

	HRESULT SetupOffscreenRendering();

	HRESULT SetupNV12Converter();

	void DrawSharedCaptureWindow();

	void GetParamsFromTexture();

public:
	FrameGenerator() :
		_width(0),
		_height(0),
		_captureTextureWidth(0),
		_captureTextureHeight(0),
		_captureWindowWidth(0),
		_captureWindowHeight(0)
	{
	}

	~FrameGenerator()
	{
		if (_sharedCaptureWindowHandle != NULL
			&& _sharedCaptureWindowHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_sharedCaptureWindowHandle);
			_sharedCaptureWindowHandle = NULL;
		}

		if (_sharedParamsHandle != NULL
			&& _sharedParamsHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_sharedParamsHandle);
			_sharedParamsHandle = NULL;
		}
	}

	HRESULT EnsureRenderTarget(UINT width, UINT height);
	HRESULT Generate(IMFSample* sample, REFGUID format, IMFSample** outSample);
};