#include "mf_capture_monitor.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/windows.graphics.directx.direct3d11.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <vector>
#include <mutex>

#define XALIGN(x, a) (((x) + (a)-1) & ~((a)-1))

extern "C"
{
	HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);
}

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
	IDirect3DDxgiInterfaceAccess : ::IUnknown
{
	virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

class MFMonitorCapture::Impl
{
public:
	Impl()
	{
		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &m_pD3DDevice, nullptr, &m_pD3DContext);
		IDXGIDevice* dxgi_device = reinterpret_cast<IDXGIDevice*>(m_pD3DDevice);
		winrt::com_ptr<::IInspectable> d3d11_device;
		CreateDirect3D11DeviceFromDXGIDevice(dxgi_device, d3d11_device.put());
		m_DirectDevice = d3d11_device.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

		EnumDisplayMonitors(
			nullptr, nullptr,
			[](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
				auto& monitor_list = *reinterpret_cast<std::vector<HMONITOR>*>(lparam);
				monitor_list.push_back(hmon);
				return TRUE;
			},
			reinterpret_cast<LPARAM>(&m_pMonitorList));
	}

	~Impl()
	{
		m_DirectDevice.Close();
	}

	int get_monitor_count()
	{
		EnumDisplayMonitors(
			nullptr, nullptr,
			[](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
				auto& monitor_list = *reinterpret_cast<std::vector<HMONITOR>*>(lparam);
				monitor_list.push_back(hmon);
				return TRUE;
			},
			reinterpret_cast<LPARAM>(&m_pMonitorList));
		return (int)m_pMonitorList.size();
	}

	HMONITOR get_monitor_handle(int index)
	{
		if (index < 0 || index >= m_pMonitorList.size())
		{
			return nullptr;
		}
		return m_pMonitorList[index];
	}

	std::string get_monitor_name(int index)
	{
		if (index < 0 || index >= m_pMonitorList.size())
		{
			return std::string();
		}
		MONITORINFOEXA mi;
		mi.cbSize = sizeof(mi);
		GetMonitorInfoA(m_pMonitorList[index], &mi);
		return std::string(mi.szDevice);
	}

	void get_monitor_resolution(HMONITOR hmon, int& width, int& height)
	{
		MONITORINFOEXA mi;
		mi.cbSize = sizeof(mi);
		GetMonitorInfoA(hmon, &mi);
		width = mi.rcMonitor.right - mi.rcMonitor.left;
		height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	bool start(HMONITOR hmon, bool show_cursor, MONITOR_COLOR_FORMAT format)
	{
		if (m_Session)
		{
			return false;
		}
		auto activation_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
		auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
		HRESULT hr = interop_factory->CreateForMonitor(hmon, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));
		if (FAILED(hr))
		{
			return false;
		}
		m_MonitorSize = item.Size();
		m_FramePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(m_DirectDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, m_MonitorSize);
		m_Session = m_FramePool.CreateCaptureSession(item);

		m_OutputFormat = format;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_MonitorSize.Width;
		desc.Height = m_MonitorSize.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		if (m_OutputFormat == MONITOR_D3D11)
		{
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
		}
		else
		{
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		}
		desc.MiscFlags = 0;
		hr = m_pD3DDevice->CreateTexture2D(&desc, nullptr, &m_pCopyTexture);
		if (FAILED(hr))
		{
			return false;
		}

		if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
			L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled"))
		{
			m_Session.IsCursorCaptureEnabled(show_cursor);
		}
		m_FrameArrived = m_FramePool.FrameArrived(winrt::auto_revoke, { this, &MFMonitorCapture::Impl::on_frame_arrived });
		m_Closed = item.Closed(winrt::auto_revoke, { this, &MFMonitorCapture::Impl::on_closed });
		m_Session.StartCapture();
		return true;
	}

	void stop()
	{
		m_FrameArrived.revoke();
		m_Closed.revoke();
		if (m_FramePool)
		{
			m_FramePool.Close();
			m_FramePool = nullptr;
		}
		if (m_Session)
		{
			m_Session.Close();
			m_Session = nullptr;
		}
		if (m_pCopyTexture)
		{
			m_pCopyTexture->Release();
			m_pCopyTexture = nullptr;
		}
	}

	bool capture(OutputMonitorData& output_data)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		{
			std::lock_guard lock(m_mtTextureLock);
			if (m_pFullScreenTexture)
			{			
				m_pFullScreenTexture->GetDesc(&desc);
				if (m_MonitorSize.Width != desc.Width || m_MonitorSize.Height != desc.Height)
				{
					m_bChangingSize = true;
				}
				else
				{
					if (m_bChangingSize)
					{
						if (m_OutputFormat == MONITOR_BGRA)
						{
							int stride = XALIGN(desc.Width * 4, 64);
							delete[] output_data.data;
							output_data.data = new uint8_t[stride * m_MonitorSize.Height];
						}
						D3D11_TEXTURE2D_DESC copy_desc = {};
						m_pCopyTexture->GetDesc(&copy_desc);
						copy_desc.Width = m_MonitorSize.Width;
						copy_desc.Height = m_MonitorSize.Height;
						m_pCopyTexture->Release();
						m_pD3DDevice->CreateTexture2D(&copy_desc, nullptr, &m_pCopyTexture);
						m_bChangingSize = false;
					}
					if (m_pCopyTexture)
					{
						m_pD3DContext->CopyResource(m_pCopyTexture, m_pFullScreenTexture);
					}
				}
			}
		}
		if (m_OutputFormat == MONITOR_D3D11)
		{
			output_data.width = desc.Width;
			output_data.height = desc.Height;
			output_data.stride = desc.Width * 4;
			output_data.format = MONITOR_D3D11;
			output_data.data = reinterpret_cast<uint8_t*>(m_pCopyTexture);
			output_data.size = 0;
		}
		else
		{
			output_data.width = desc.Width;
			output_data.height = desc.Height;
			output_data.format = MONITOR_BGRA;
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (m_pCopyTexture)
			{
				if (SUCCEEDED(m_pD3DContext->Map(m_pCopyTexture, 0, D3D11_MAP_READ, 0, &mapped)))
				{
					output_data.stride = mapped.RowPitch;
					output_data.size = mapped.RowPitch * desc.Height;
					memcpy(output_data.data, mapped.pData, output_data.size);
					m_pD3DContext->Unmap(m_pCopyTexture, 0);
				}
			}
		}
		return true;
	}

private:
	void on_frame_arrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const& args)
	{
		auto frame = sender.TryGetNextFrame();
		auto frameContentSize = frame.ContentSize();
		if (m_MonitorSize.Width != frameContentSize.Width || m_MonitorSize.Height != frameContentSize.Height)
		{
			m_FramePool.Recreate(m_DirectDevice,
				winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1,
				frameContentSize);
			m_MonitorSize = frameContentSize;
		}
		std::lock_guard lock(m_mtTextureLock);
		auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
		access->GetInterface(IID_PPV_ARGS(&m_pFullScreenTexture));
	}

	void on_closed(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& sender, winrt::Windows::Foundation::IInspectable const& args)
	{
		std::lock_guard lock(m_mtTextureLock);
		m_pFullScreenTexture = nullptr;
	}

	ID3D11Device* m_pD3DDevice{ nullptr };
	ID3D11DeviceContext* m_pD3DContext{ nullptr };
	ID3D11Texture2D* m_pFullScreenTexture{ nullptr };
	ID3D11Texture2D* m_pCopyTexture{ nullptr };
	MONITOR_COLOR_FORMAT m_OutputFormat{ MONITOR_COLOR_FORMAT::MONITOR_NONE };
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_DirectDevice{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_FramePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_Session{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_FrameArrived;
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker m_Closed;
	winrt::Windows::Graphics::SizeInt32 m_MonitorSize{ 0, 0 };
	std::mutex m_mtTextureLock;
	bool m_bChangingSize{ false };
	std::vector<HMONITOR> m_pMonitorList;
};


MFMonitorCapture::MFMonitorCapture()
{
	impl_ = new Impl();
}

MFMonitorCapture::~MFMonitorCapture()
{
	delete impl_;
}

int MFMonitorCapture::get_monitor_count()
{
	return impl_->get_monitor_count();
}

void* MFMonitorCapture::get_monitor_handle(int index)
{
	return impl_->get_monitor_handle(index);
}

std::string MFMonitorCapture::get_monitor_name(int index)
{
	return impl_->get_monitor_name(index);
}

void MFMonitorCapture::get_monitor_resolution(void* hmon, int& width, int& height)
{
	impl_->get_monitor_resolution((HMONITOR)hmon, width, height);
}

bool MFMonitorCapture::start(void* hmon, bool show_cursor, MONITOR_COLOR_FORMAT format)
{
	return impl_->start((HMONITOR)hmon, show_cursor, format);
}

void MFMonitorCapture::stop()
{
	impl_->stop();
}

bool MFMonitorCapture::capture( OutputMonitorData& output_data)
{
	return impl_->capture(output_data);
}