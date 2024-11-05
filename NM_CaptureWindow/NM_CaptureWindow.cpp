
#include <windows.h>
#include <sddl.h>
#include "NM_CaptureWindow.h"

/****************************************************************/
/*  MediaFoundation Virtual Camera Function Start               */
/****************************************************************/
void NM_CaptureWindow::CreateVirtualCamera()
{
	HRESULT hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
		MFVirtualCameraLifetime_Session, 
		MFVirtualCameraAccess_CurrentUser,
		TEXT("NM_Capture_Window_Vcam_Sample"), 
		TEXT("{3f5ae681-e9b9-4e19-a3ed-06463549719d}"), 
		nullptr, 0, _vcam.put());

	if (hr == S_OK)
	{
		_vcam->Start(nullptr);
	}
}

void NM_CaptureWindow::StopVirtualCamera()
{
	if (_vcam != nullptr)
	{
		_vcam->Stop();
		_vcam->Shutdown();
		_vcam->Release();
		_vcam = nullptr;
	}
}
/****************************************************************/
/*  MediaFoundation Virtual Camera Function End                 */
/****************************************************************/

/****************************************************************/
/*  DirectX Function Start                                      */
/****************************************************************/
template<typename T>
auto getDXGIInterfaceFromObject(winrt::Windows::Foundation::IInspectable const& object) {
	auto access = object.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	com_ptr<T> result;
	check_hresult(access->GetInterface(guid_of<T>(), result.put_void()));
	return result;
}

void NM_CaptureWindow::CreateDirect3DDeviceForCapture() {
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	if (_dxDeviceForCapture != nullptr) {
		_dxDeviceForCapture.Close();
	}

	D3D_FEATURE_LEVEL d3dFeatures[1] = {
		D3D_FEATURE_LEVEL_11_1
	};
	D3D_FEATURE_LEVEL resultFeature;
	check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, createDeviceFlags, d3dFeatures, 1, D3D11_SDK_VERSION,
		_d3dDevice.put(), &resultFeature, _deviceCtxForCapture.put()));
	com_ptr<IDXGIDevice> dxgiDevice = _d3dDevice.as<IDXGIDevice>();
	com_ptr<::IInspectable> device = nullptr;
	check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), device.put()));
	_dxDeviceForCapture = device.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

void NM_CaptureWindow::CreateSharedCaptureWindowTexture() {
	if (_d3dDevice == nullptr) {
		return;
	}

	//他プロセスと共有するためのテクスチャ
	D3D11_TEXTURE2D_DESC bufferTextureDesc;

	bufferTextureDesc.Width = MAX_SOURCE_WIDTH;
	bufferTextureDesc.Height = MAX_SOURCE_HEIGHT;
	bufferTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bufferTextureDesc.ArraySize = 1;
	bufferTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufferTextureDesc.CPUAccessFlags = 0;
	bufferTextureDesc.MipLevels = 1;
	bufferTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	bufferTextureDesc.SampleDesc.Count = 1;
	bufferTextureDesc.SampleDesc.Quality = 0;
	bufferTextureDesc.Usage = D3D11_USAGE_DEFAULT;

	check_hresult(_d3dDevice->CreateTexture2D(&bufferTextureDesc, 0, _bufferTextureForCapture.put()));

	com_ptr<IDXGIResource1> sharedCaptureWindowResource;
	_bufferTextureForCapture->QueryInterface(IID_PPV_ARGS(sharedCaptureWindowResource.put()));

	// Session0であるFrameServerで共有テクスチャにアクセスできるようにするため、
	// SECURITY_ATTRIBUTESを設定する必要がある。
	SECURITY_ATTRIBUTES secAttr;
	secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	secAttr.bInheritHandle = FALSE;
	secAttr.lpSecurityDescriptor = NULL;

	// Local Serviceにのみ読み取りのアクセス権（今回のFrameServerのパイプラインならこれで十分）
	// Interactive User（現在のユーザ）に全アクセス権を付与するなら(A;;GA;;;IU)を追加する。
	if (ConvertStringSecurityDescriptorToSecurityDescriptor(
		TEXT("D:(A;;GR;;;LS)"),
		SDDL_REVISION_1, &(secAttr.lpSecurityDescriptor), NULL)) {

		HRESULT hr = sharedCaptureWindowResource->CreateSharedHandle(&secAttr,
			DXGI_SHARED_RESOURCE_READ,
			TEXT("Global\\NM_Capture_Window"),
			&_sharedCaptureWindowHandle);
		com_ptr<IDXGIKeyedMutex> mutex;
		_bufferTextureForCapture.as(mutex);
		mutex->AcquireSync(0, INFINITE);
		mutex->ReleaseSync(MUTEX_KEY);
	}
}

void NM_CaptureWindow::CreateSharedParamsTexture() {
	if (_d3dDevice == nullptr) {
		return;
	}

	D3D11_TEXTURE2D_DESC bufferParamsDesc;
	bufferParamsDesc.Width = SHARED_PARAMS_BUF;
	bufferParamsDesc.Height = SHARED_PARAMS_BUF;
	bufferParamsDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bufferParamsDesc.ArraySize = 1;
	bufferParamsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufferParamsDesc.CPUAccessFlags = 0;
	bufferParamsDesc.MipLevels = 1;
	bufferParamsDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	bufferParamsDesc.SampleDesc.Count = 1;
	bufferParamsDesc.SampleDesc.Quality = 0;
	bufferParamsDesc.Usage = D3D11_USAGE_DEFAULT;

	check_hresult(_d3dDevice->CreateTexture2D(&bufferParamsDesc, NULL, _bufferParams.put()));

	bufferParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferParamsDesc.MiscFlags = 0;
	bufferParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
	check_hresult(_d3dDevice->CreateTexture2D(&bufferParamsDesc, NULL, _cpuBufferParams.put()));

	com_ptr<IDXGIResource1> sharedParamsResource;
	_bufferParams->QueryInterface(IID_PPV_ARGS(sharedParamsResource.put()));

	// Session0であるFrameServerで共有テクスチャにアクセスできるようにするため、
	// SECURITY_ATTRIBUTESを設定する必要がある。
	SECURITY_ATTRIBUTES secAttr;
	secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	secAttr.bInheritHandle = FALSE;
	secAttr.lpSecurityDescriptor = NULL;

	// Local Serviceにのみ読み取りのアクセス権（今回のFrameServerのパイプラインならこれで十分）
	// Interactive User（現在のユーザ）に全アクセス権を付与するなら(A;;GA;;;IU)を追加する。
	if (ConvertStringSecurityDescriptorToSecurityDescriptor(
		TEXT("D:(A;;GR;;;LS)"),
		SDDL_REVISION_1, &(secAttr.lpSecurityDescriptor), NULL)) {

		HRESULT hr = sharedParamsResource->CreateSharedHandle(&secAttr,
			DXGI_SHARED_RESOURCE_READ,
			TEXT("Global\\NM_Capture_Window_Params"),
			&_sharedParamsHandle);
		com_ptr<IDXGIKeyedMutex> mutex;
		_bufferParams.as(mutex);
		mutex->AcquireSync(0, INFINITE);
		mutex->ReleaseSync(MUTEX_KEY);
	}
}

void NM_CaptureWindow::UpdateSharedParams() {
	if (_bufferParams == nullptr) {
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mapFromTexture;
	_deviceCtxForCapture->Map(_cpuBufferParams.get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapFromTexture);

	int memIdx = 0;
	CopyMemory((PVOID)((char*)mapFromTexture.pData + memIdx), (PVOID)(&_capWinSize.Width), sizeof(int32_t));
	memIdx += sizeof(int32_t);
	CopyMemory((PVOID)((char*)mapFromTexture.pData + memIdx), (PVOID)(&_capWinSize.Height), sizeof(int32_t));
	memIdx += sizeof(int32_t);

	_deviceCtxForCapture->Unmap(_cpuBufferParams.get(), 0);

	com_ptr<IDXGIKeyedMutex> mutex;
	_bufferParams.as(mutex);
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_deviceCtxForCapture->CopyResource(_bufferParams.get(), _cpuBufferParams.get());
	mutex->ReleaseSync(MUTEX_KEY);
}
/****************************************************************/
/*  DirectX Function End                                        */
/****************************************************************/

/****************************************************************/
/*  winRT GraphicsCapture Function Start                        */
/****************************************************************/
bool NM_CaptureWindow::IsCapturing() { return _framePoolForCapture != nullptr; }

void NM_CaptureWindow::StopCapture() {
	if (IsCapturing()) {
		_frameArrivedForCapture.revoke();
		_captureSession = nullptr;
		_framePoolForCapture.Close();
		_framePoolForCapture = nullptr;
		_graphicsCaptureItem = nullptr;
	}

	if (_sharedParamsHandle != NULL 
		&& _sharedParamsHandle != INVALID_HANDLE_VALUE) {

		CloseHandle(_sharedParamsHandle);
		_sharedParamsHandle = NULL;
	}

	if (_sharedCaptureWindowHandle != NULL
		&& _sharedCaptureWindowHandle != INVALID_HANDLE_VALUE) {

		CloseHandle(_sharedCaptureWindowHandle);
		_sharedCaptureWindowHandle = NULL;
	}
}

void NM_CaptureWindow::SetTargetWindowForCapture(HWND targetWindow)
{
	namespace abi = ABI::Windows::Graphics::Capture;
	if (targetWindow == NULL) {
		return;
	}

	auto factory = get_activation_factory<GraphicsCaptureItem>();
	auto interop = factory.as<::IGraphicsCaptureItemInterop>();
	check_hresult(interop->CreateForWindow(targetWindow, guid_of<abi::IGraphicsCaptureItem>(),
		reinterpret_cast<void**>(put_abi(_graphicsCaptureItem))));
	_capWinSize = _graphicsCaptureItem.Size();
	_capWinSizeInTexture.right = _capWinSize.Width;
	_capWinSizeInTexture.bottom = _capWinSize.Height;
	UpdateSharedParams();
	_framePoolForCapture = Direct3D11CaptureFramePool::CreateFreeThreaded(_dxDeviceForCapture,
		DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, _capWinSize);
	_frameArrivedForCapture = _framePoolForCapture.FrameArrived(auto_revoke, { this, &NM_CaptureWindow::OnFrameArrived });
	_captureSession = _framePoolForCapture.CreateCaptureSession(_graphicsCaptureItem);
	//IsCursorCaptureEnabledでカーソルもキャプチャするか指定できる。
	_captureSession.IsCursorCaptureEnabled(false);
	_captureSession.StartCapture();
}

void NM_CaptureWindow::OnFrameArrived(Direct3D11CaptureFramePool const& sender,
	winrt::Windows::Foundation::IInspectable const& args)
{
	auto frame = sender.TryGetNextFrame();

	SizeInt32 itemSize = frame.ContentSize();
	if (itemSize.Width <= 0) {
		itemSize.Width = 1;
	}
	if (itemSize.Width > MAX_SOURCE_WIDTH)
	{
		itemSize.Width = MAX_SOURCE_WIDTH;
	}
	if (itemSize.Height <= 0) {
		itemSize.Height = 1;
	}
	if (itemSize.Height > MAX_SOURCE_HEIGHT)
	{
		itemSize.Height = MAX_SOURCE_HEIGHT;
	}

	if (itemSize.Width != _capWinSize.Width
		|| itemSize.Height != _capWinSize.Height) {
		_capWinSize = itemSize;
		_capWinSizeInTexture.right = _capWinSize.Width;
		_capWinSizeInTexture.bottom = _capWinSize.Height;
		UpdateSharedParams();
		_framePoolForCapture.Recreate(_dxDeviceForCapture,
			DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, _capWinSize);
	}

	//キャプチャしたフレームから得られるテクスチャをバッファテクスチャにGPU上でデータコピー
	if (_bufferTextureForCapture != nullptr) {
		com_ptr<ID3D11Texture2D> texture2D = getDXGIInterfaceFromObject<::ID3D11Texture2D>(frame.Surface());
		com_ptr<IDXGIKeyedMutex> mutex;
		_bufferTextureForCapture.as(mutex);
		mutex->AcquireSync(MUTEX_KEY, INFINITE);
		_deviceCtxForCapture->CopySubresourceRegion(_bufferTextureForCapture.get(), 0, 0, 0, 0, texture2D.get(), 0, &_capWinSizeInTexture);
		_deviceCtxForCapture->Flush();
		mutex->ReleaseSync(MUTEX_KEY);
	}
}
/****************************************************************/
/*  winRT GraphicsCapture Function End                          */
/****************************************************************/
