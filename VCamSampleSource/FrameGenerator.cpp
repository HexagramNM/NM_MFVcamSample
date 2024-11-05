
#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "D3dcompiler.h"

#define HLSL_EXTERNAL_INCLUDE(...) #__VA_ARGS__

// Embeded hlsl shader source code.
const char* hlslCode =
#include "SpriteShader.hlsl"
;


HRESULT FrameGenerator::SetupD3D11Device() {
	UINT resetToken;
	RETURN_IF_FAILED(MFCreateDXGIDeviceManager(&resetToken, _dxgiManager.put()));

	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	createDeviceFlags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	wil::com_ptr_nothrow<ID3D11Device> device;
	D3D_FEATURE_LEVEL d3dFeatures[7] = {
		D3D_FEATURE_LEVEL_11_1
	};

	RETURN_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, createDeviceFlags, d3dFeatures, 1, D3D11_SDK_VERSION,
		device.put(), nullptr, _textureDeviceContext.put()));

	_textureDeviceContext->QueryInterface(IID_PPV_ARGS(_deviceMutex.put()));
	_deviceMutex->SetMultithreadProtected(true);

	_dxgiManager->ResetDevice(device.get(), resetToken);

	device->QueryInterface(IID_PPV_ARGS(_textureDevice.put()));

	return S_OK;
}

HRESULT FrameGenerator::CreateRenderTargetResources()
{
	assert(_renderTarget);
	RETURN_IF_FAILED(_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &_whiteBrush));

	RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&_dwrite));
	RETURN_IF_FAILED(_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 40, L"", &_textFormat));
	RETURN_IF_FAILED(_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	RETURN_IF_FAILED(_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	return S_OK;
}

// CPUからアクセス可能なテクスチャを作成し、
// ハンドルから共有されたテクスチャを取得。
HRESULT FrameGenerator::CreateSharedCaptureWindowTexture()
{
	if (_sharedCaptureWindowTexture != nullptr)
	{
		return S_OK;
	}

	RETURN_IF_FAILED(_textureDevice->OpenSharedResourceByName(
		TEXT("Global\\NM_Capture_Window"), DXGI_SHARED_RESOURCE_READ, 
		IID_PPV_ARGS(_sharedCaptureWindowTexture.put())));

	D3D11_TEXTURE2D_DESC textureDesc;
	_sharedCaptureWindowTexture->GetDesc(&textureDesc);
	_captureTextureWidth = textureDesc.Width;
	_captureTextureHeight = textureDesc.Height;

	return S_OK;
}

HRESULT FrameGenerator::CreateSharedParamsTexture()
{
	if (_sharedParamsTexture != nullptr)
	{
		return S_OK;
	}

	D3D11_TEXTURE2D_DESC bufferTextureDesc;

	RETURN_IF_FAILED(_textureDevice->OpenSharedResourceByName(
		TEXT("Global\\NM_Capture_Window_Params"), DXGI_SHARED_RESOURCE_READ, 
		IID_PPV_ARGS(_sharedParamsTexture.put())));

	D3D11_TEXTURE2D_DESC textureDesc;
	_sharedParamsTexture->GetDesc(&textureDesc);
	UINT sharedParamsTextureWidth = textureDesc.Width;
	UINT sharedParamsTextureHeight = textureDesc.Height;

	bufferTextureDesc.Width = sharedParamsTextureWidth;
	bufferTextureDesc.Height = sharedParamsTextureHeight;
	bufferTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bufferTextureDesc.ArraySize = 1;
	bufferTextureDesc.BindFlags = 0;
	bufferTextureDesc.MipLevels = 1;
	bufferTextureDesc.SampleDesc.Count = 1;
	bufferTextureDesc.SampleDesc.Quality = 0;
	bufferTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	bufferTextureDesc.MiscFlags = 0;
	bufferTextureDesc.Usage = D3D11_USAGE_STAGING;
	RETURN_IF_FAILED(_textureDevice->CreateTexture2D(&bufferTextureDesc, 0, 
		_cpuParamsTexture.put()));

	return S_OK;
}

HRESULT FrameGenerator::SetupOffscreenRendering() {
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	CD3D11_TEXTURE2D_DESC desc
	(
		dxgiFormat,
		_width,
		_height,
		1,
		1,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		D3D11_USAGE_DEFAULT,
		0,
		1
	);
	RETURN_IF_FAILED(_textureDevice->CreateTexture2D(&desc, nullptr, _texture.put()));

	CD3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc(D3D11_RTV_DIMENSION_TEXTURE2D, dxgiFormat);
	RETURN_IF_FAILED(_textureDevice->CreateRenderTargetView(_texture.get(),
		&renderTargetViewDesc, _renderTargetView.put()));

	CD3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc(D3D11_SRV_DIMENSION_TEXTURE2D, dxgiFormat);
	RETURN_IF_FAILED(_textureDevice->CreateShaderResourceView(_sharedCaptureWindowTexture.get(),
		&shaderResourceViewDesc, _shaderResourceView.put()));

	_textureDeviceContext->OMSetRenderTargets(1, _renderTargetView.addressof(), nullptr);
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)_width, (float)_height, 0.0f, 1.0f };
	_textureDeviceContext->RSSetViewports(1, &vp);

	size_t hlslSize = std::strlen(hlslCode);
	wil::com_ptr_t<ID3DBlob> compiledVS;
	RETURN_IF_FAILED(D3DCompile(hlslCode, hlslSize, nullptr, nullptr, nullptr,
		"VS", "vs_5_0", 0, 0, compiledVS.put(), nullptr));

	wil::com_ptr_t<ID3DBlob> compiledPS;
	RETURN_IF_FAILED(D3DCompile(hlslCode, hlslSize, nullptr, nullptr, nullptr,
		"PS", "ps_5_0", 0, 0, compiledPS.put(), nullptr));

	RETURN_IF_FAILED(_textureDevice->CreateVertexShader(compiledVS->GetBufferPointer(),
		compiledVS->GetBufferSize(), nullptr, _spriteVS.put()));

	RETURN_IF_FAILED(_textureDevice->CreatePixelShader(compiledPS->GetBufferPointer(),
		compiledPS->GetBufferSize(), nullptr, _spritePS.put()));

	D3D11_INPUT_ELEMENT_DESC layout[2] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXUV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	RETURN_IF_FAILED(_textureDevice->CreateInputLayout(layout, 2, compiledVS->GetBufferPointer(),
		compiledVS->GetBufferSize(), _spriteInputLayout.put()));

	return S_OK;
}

HRESULT FrameGenerator::SetupNV12Converter() {
	// create GPU RGB => NV12 converter
	RETURN_IF_FAILED(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_converter)));

	wil::com_ptr_nothrow<IMFAttributes> atts;
	RETURN_IF_FAILED(_converter->GetAttributes(&atts));
	TraceMFAttributes(atts.get(), L"VideoProcessorMFT");

	MFT_OUTPUT_STREAM_INFO info{};
	RETURN_IF_FAILED(_converter->GetOutputStreamInfo(0, &info));
	WINTRACE(L"FrameGenerator::EnsureRenderTarget CLSID_VideoProcessorMFT flags:0x%08X size:%u alignment:%u", info.dwFlags, info.cbSize, info.cbAlignment);

	wil::com_ptr_nothrow<IMFMediaType> inputType;
	RETURN_IF_FAILED(MFCreateMediaType(&inputType));
	inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, _width, _height);
	RETURN_IF_FAILED(_converter->SetInputType(0, inputType.get(), 0));

	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, _width, _height);
	RETURN_IF_FAILED(_converter->SetOutputType(0, outputType.get(), 0));

	// make sure the video processor works on GPU
	RETURN_IF_FAILED(_converter->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)_dxgiManager.get()));

	return S_OK;
}

HRESULT FrameGenerator::EnsureRenderTarget(UINT width, UINT height)
{
	if (!_dxgiManager) {
		_width = width;
		_height = height;

		RETURN_IF_FAILED(SetupD3D11Device());

		RETURN_IF_FAILED(CreateSharedCaptureWindowTexture());

		RETURN_IF_FAILED(CreateSharedParamsTexture());

		RETURN_IF_FAILED(SetupOffscreenRendering());

		RETURN_IF_FAILED(SetupNV12Converter());
	}

	_prevTime = MFGetSystemTime();
	_frame = 0;
	return S_OK;
}

void FrameGenerator::DrawSharedCaptureWindow()
{
	if (_sharedCaptureWindowTexture == nullptr)
	{
		return;
	}

	float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	_textureDeviceContext->ClearRenderTargetView(_renderTargetView.get(), color);

	struct VertexType
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT2 Tex;
	};

	float xPosRate = 1.0f;
	float yPosRate = 1.0f;
	float rectRate = static_cast<float>(_width) / static_cast<float>(_height);
	float floatWindowWidth = static_cast<float>(_captureWindowWidth);
	float floatWindowHeight = static_cast<float>(_captureWindowHeight);

	if (floatWindowWidth > floatWindowHeight * rectRate) {
		yPosRate = floatWindowHeight * rectRate / floatWindowWidth;
	}
	else {
		xPosRate = floatWindowWidth / (floatWindowHeight * rectRate);
	}

	float widthTextureRate = static_cast<float>(_captureWindowWidth) 
		/ static_cast<float>(_captureTextureWidth);
	float heightTextureRate = static_cast<float>(_captureWindowHeight) 
		/ static_cast<float>(_captureTextureHeight);
	
	VertexType v[4] = {
		{{-xPosRate, yPosRate, 0}, {0, 0}},
		{{xPosRate, yPosRate, 0}, {widthTextureRate, 0}},
		{{-xPosRate, -yPosRate, 0}, {0, heightTextureRate}},
		{{xPosRate, -yPosRate, 0}, {widthTextureRate, heightTextureRate}}
	};

	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.ByteWidth = sizeof(v);
	vbDesc.MiscFlags = 0;
	vbDesc.StructureByteStride = 0;
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.CPUAccessFlags = 0;

	wil::com_ptr_nothrow<ID3D11Buffer> vb;
	D3D11_SUBRESOURCE_DATA initData = {
		&v[0], sizeof(v), 0
	};

	_textureDevice->CreateBuffer(&vbDesc, &initData, &vb);

	UINT stride = sizeof(VertexType);
	UINT offset = 0;
	_textureDeviceContext->IASetVertexBuffers(0, 1, vb.addressof(), &stride, &offset);
	_textureDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_textureDeviceContext->VSSetShader(_spriteVS.get(), 0, 0);
	_textureDeviceContext->PSSetShader(_spritePS.get(), 0, 0);
	_textureDeviceContext->IASetInputLayout(_spriteInputLayout.get());

	com_ptr<IDXGIKeyedMutex> mutexCapture;
	_sharedCaptureWindowTexture.as(mutexCapture);
	mutexCapture->AcquireSync(MUTEX_KEY, INFINITE);
	_textureDeviceContext->PSSetShaderResources(0, 1, _shaderResourceView.addressof());
	_textureDeviceContext->Draw(4, 0);
	_textureDeviceContext->Flush();
	mutexCapture->ReleaseSync(MUTEX_KEY);

}

void FrameGenerator::GetParamsFromTexture()
{
	if (_sharedParamsTexture == nullptr || _cpuParamsTexture == nullptr)
	{
		return;
	}
	com_ptr<IDXGIKeyedMutex> mutex;
	_sharedParamsTexture.as(mutex);
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_textureDeviceContext->CopyResource(_cpuParamsTexture.get(), _sharedParamsTexture.get());
	mutex->ReleaseSync(MUTEX_KEY);

	com_ptr<IDXGISurface> dxgiSurface;
	_cpuParamsTexture->QueryInterface(IID_PPV_ARGS(dxgiSurface.put()));

	DXGI_MAPPED_RECT mapFromTexture;
	dxgiSurface->Map(&mapFromTexture, DXGI_MAP_READ);
	int memIdx = 0;
	CopyMemory((PVOID)&_captureWindowWidth, (PVOID)(mapFromTexture.pBits + memIdx), sizeof(int));
	memIdx += sizeof(int32_t);
	CopyMemory((PVOID)&_captureWindowHeight, (PVOID)(mapFromTexture.pBits + memIdx), sizeof(int));
	memIdx += sizeof(int32_t);
	dxgiSurface->Unmap();
}

HRESULT FrameGenerator::Generate(IMFSample* sample, REFGUID format, IMFSample** outSample)
{
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	_deviceMutex->Enter();
	GetParamsFromTexture();

	DrawSharedCaptureWindow();
	_deviceMutex->Leave();
	// render something on image common to CPU & GPU
	/*if (_renderTarget)
	{
		_renderTarget->BeginDraw();
		_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 1));

		DrawSharedCaptureWindow();

		// draw resolution at center
		// note: we could optimize here and compute layout only once if text doesn't change (depending on the font, etc.)
#ifdef _DEBUG
		if (_textFormat && _dwrite && _whiteBrush)
		{
			wchar_t text[127];
			wchar_t fmt[15];
			if (format == MFVideoFormat_NV12)
			{
				lstrcpy(fmt, L"NV12 (GPU)");
			}
			else
			{
				lstrcpy(fmt, L"RGB32 (GPU)");
			}

#define FRAMES_FOR_FPS 60 // number of frames to wait to compute fps from last measure
#define NS_PER_MS 10000
#define MS_PER_S 1000

			if (!_fps || !(_frame % FRAMES_FOR_FPS))
			{
				auto time = MFGetSystemTime();
				_fps = (UINT)(MS_PER_S * NS_PER_MS * FRAMES_FOR_FPS / (time - _prevTime));
				_prevTime = time;
			}

			auto len = wsprintf(text, L"Format: %s\nFrame#: %I64i\nFps: %u\nResolution: %u x %u", fmt, _frame, _fps, _width, _height);

			wil::com_ptr_nothrow<IDWriteTextLayout> layout;
			RETURN_IF_FAILED(_dwrite->CreateTextLayout(text, len, _textFormat.get(), (FLOAT)_width, (FLOAT)_height, &layout));

			_renderTarget->DrawTextLayout(D2D1::Point2F(0, 0), layout.get(), _whiteBrush.get());
		}
#endif
		_renderTarget->EndDraw();
	}*/

	// build a sample using either D3D/DXGI (GPU) or WIC (CPU)
	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;

	// remove all existing buffers
	RETURN_IF_FAILED(sample->RemoveAllBuffers());

	// create a buffer from this and add to sample
	RETURN_IF_FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), _texture.get(), 0, 0, &mediaBuffer));
	RETURN_IF_FAILED(sample->AddBuffer(mediaBuffer.get()));

	// if we're on GPU & format is not RGB, convert using GPU
	if (format == MFVideoFormat_NV12)
	{
		assert(_converter);
		RETURN_IF_FAILED(_converter->ProcessInput(0, sample, 0));

		// let converter build the sample for us, note it works because we gave it the D3DManager
		MFT_OUTPUT_DATA_BUFFER buffer = {};
		DWORD status = 0;
		RETURN_IF_FAILED(_converter->ProcessOutput(0, 1, &buffer, &status));
		*outSample = buffer.pSample;
	}
	else
	{
		sample->AddRef();
		*outSample = sample;
	}

	_frame++;
	return S_OK;
}
