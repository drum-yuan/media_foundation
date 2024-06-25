#include "mf_capture_camera.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>
#include <codecvt>
#include <map>

class MFCameraCapture::Impl
{
public:
	Impl()
	{
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		MFStartup(MF_VERSION);

		HRESULT hr = MFCreateAttributes(&m_pAttributes, 1);
		if (FAILED(hr))
		{
			return;
		}
		hr = m_pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
		if (FAILED(hr))
		{
			m_pAttributes->Release();
			m_pAttributes = nullptr;
			return;
		}

		hr = MFEnumDeviceSources(m_pAttributes, &m_pMFActivates, &m_iCameraCount);
	}

	~Impl()
	{
		if (m_pAttributes)
		{
			m_pAttributes->Release();
			m_pAttributes = nullptr;
		}
		if (m_pMFActivates)
		{
			for (UINT32 i = 0; i < m_iCameraCount; i++)
			{
				m_pMFActivates[i]->Release();
			}
			CoTaskMemFree(m_pMFActivates);
		}
		MFShutdown();
		CoUninitialize();
	}

	int get_camera_count()
	{
		if (m_pMFActivates)
		{
			for (UINT32 i = 0; i < m_iCameraCount; i++)
			{
				m_pMFActivates[i]->Release();
			}
			CoTaskMemFree(m_pMFActivates);
		}
		MFEnumDeviceSources(m_pAttributes, &m_pMFActivates, &m_iCameraCount);
		return m_iCameraCount;
	}

	std::string get_camera_id(int index)
	{
		if (m_pMFActivates == nullptr)
		{
			return std::string();
		}
		WCHAR* guid = 0;
		UINT32 guid_len = 255;
		m_pMFActivates[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &guid, &guid_len);
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
		return convert.to_bytes(guid);
	}

	std::string get_camera_name(int index)
	{
		if (m_pMFActivates == nullptr)
		{
			return std::string();
		}
		WCHAR* name = 0;
		UINT32 name_len = 255;
		m_pMFActivates[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len);
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
		return convert.to_bytes(name);
	}

	bool start(const std::string& camera_id, int& width, int& height, CAMERA_COLOR_FORMAT& format)
	{
		if (m_pMFActivates == nullptr)
		{
			return false;
		}
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return false;
		}
		IMFMediaSource* media_source = nullptr;
		HRESULT hr = m_pMFActivates[i]->ActivateObject(IID_PPV_ARGS(&media_source));
		if (FAILED(hr))
		{
			return false;
		}
		IMFSourceReader* source_reader = nullptr;
		hr = MFCreateSourceReaderFromMediaSource(media_source, m_pAttributes, &source_reader);
		if (FAILED(hr))
		{
			return false;
		}
		m_mapMediaSource[i] = media_source;
		m_mapSourceReader[i] = source_reader;
		
		int index = 0;
		int select_index = 0;
		int min_delta = INT_MAX;
		while (hr == S_OK && min_delta > 0)
		{
			IMFMediaType* native_type = NULL;
			hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &native_type);
			if (FAILED(hr))
			{
				break;
			}
			index++;
			UINT32 frame_num = 0;
			UINT32 frame_den = 0;
			hr = MFGetAttributeRatio(native_type, MF_MT_FRAME_RATE, &frame_num, &frame_den);
			if (frame_num / frame_den < 30)
			{
				continue;
			}
			UINT32 frame_width = 0;
			UINT32 frame_height = 0;
			hr = MFGetAttributeSize(native_type, MF_MT_FRAME_SIZE, &frame_width, &frame_height);
			int delta = 0;
			if (width < (int)frame_width)
				delta += (frame_width - width);
			if (height < (int)frame_height)
				delta += (frame_height - height);
			if (width > (int)frame_width)
				delta += (width - frame_width) * 2;
			if (height > (int)frame_height)
				delta += (height - frame_height) * 2;
			if (width == frame_width && height == frame_height)
				delta = 0;
			if (min_delta > delta)
			{
				min_delta = delta;
				select_index = index - 1;
			}
		}
		
		IMFMediaType* media_type = NULL;
		source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, select_index, &media_type);
		UINT32 frame_width = 0;
		UINT32 frame_height = 0;
		MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &frame_width, &frame_height);
		width = frame_width;
		height = frame_height;
		GUID subtype;
		media_type->GetGUID(MF_MT_SUBTYPE, &subtype);
		format = guid_to_camera_format(subtype);

		hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
		if (FAILED(hr))
		{
			return false;
		}
		hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, media_type);
		if (FAILED(hr))
		{
			return false;
		}
		m_mapMediaType[i] = media_type;
		return true;
	}

	void stop(const std::string& camera_id)
	{
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return;
		}
		auto it1 = m_mapMediaType.find(i);
		if (it1 != m_mapMediaType.end())
		{
			it1->second->Release();
			m_mapMediaType.erase(it1);
		}
		auto it2 = m_mapSourceReader.find(i);
		if (it2 != m_mapSourceReader.end())
		{
			it2->second->Release();
			m_mapSourceReader.erase(it2);
		}
		auto it3 = m_mapMediaSource.find(i);
		if (it3 != m_mapMediaSource.end())
		{
			it3->second->Shutdown();
			it3->second->Release();
			m_mapMediaSource.erase(it3);
		}
	}

	void get_resolution_list(const std::string& camera_id, std::vector<std::pair<int, int>>& resolution_list)
	{
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return;
		}
		auto it = m_mapSourceReader.find(i);
		if (it == m_mapSourceReader.end())
		{
			return;
		}
		int index = 0;
		HRESULT hr = S_OK;
		while (hr == S_OK)
		{
			IMFMediaType* native_type = NULL;
			hr = it->second->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &native_type);
			if (SUCCEEDED(hr))
			{
				UINT32 frame_width = 0;
				UINT32 frame_height = 0;
				hr = MFGetAttributeSize(native_type, MF_MT_FRAME_SIZE, &frame_width, &frame_height);
				if (SUCCEEDED(hr))
				{
					resolution_list.push_back(std::make_pair(frame_width, frame_height));
				}
			}
			index++;
		}
	}

	void set_property(const std::string& camera_id, CAMERA_PROPETIES prop, float value, bool use_auto)
	{
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return;
		}
		auto it = m_mapMediaSource.find(i);
		if (it == m_mapMediaSource.end())
		{
			return;
		}
		int mf_prop = camera_prop_to_mf_prop(prop);
		HRESULT hr = S_OK;
		if (mf_prop < CAMERA_PAN)
		{
			IAMVideoProcAmp* proc_amp = NULL;
			hr = it->second->QueryInterface(IID_PPV_ARGS(&proc_amp));
			if (SUCCEEDED(hr))
			{
				long min, max, step, def, caps;
				hr = proc_amp->GetRange(mf_prop, &min, &max, &step, &def, &caps);
				if (SUCCEEDED(hr))
				{
					long val = (long)floor(min + (max - min) * value);
					if (use_auto)
						val = def;
					hr = proc_amp->Set(mf_prop, val, use_auto ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);
				}
				proc_amp->Release();
			}
		}
		else
		{
			IAMCameraControl* camera_control = NULL;
			hr = it->second->QueryInterface(IID_PPV_ARGS(&camera_control));
			if (SUCCEEDED(hr))
			{
				long min, max, step, def, caps;
				hr = camera_control->GetRange(mf_prop, &min, &max, &step, &def, &caps);
				if (SUCCEEDED(hr))
				{
					long val = (long)floor(min + (max - min) * value);
					if (use_auto)
						val = def;
					hr = camera_control->Set(mf_prop, val, use_auto ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);
				}
				camera_control->Release();
			}
		}
	}

	float get_property(const std::string& camera_id, CAMERA_PROPETIES prop)
	{
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return 0.0f;
		}
		auto it = m_mapMediaSource.find(i);
		if (it == m_mapMediaSource.end())
		{
			return 0.0f;
		}
		int mf_prop = camera_prop_to_mf_prop(prop);
		HRESULT hr = S_OK;
		float value = 0.0f;
		if (mf_prop < CAMERA_PAN)
		{
			IAMVideoProcAmp* proc_amp = NULL;
			hr = it->second->QueryInterface(IID_PPV_ARGS(&proc_amp));
			if (SUCCEEDED(hr))
			{
				long min, max, step, def, caps;
				hr = proc_amp->GetRange(mf_prop, &min, &max, &step, &def, &caps);
				if (SUCCEEDED(hr))
				{
					long v = 0, f = 0;
					hr = proc_amp->Get(mf_prop, &v, &f);
					if (SUCCEEDED(hr))
					{
						value = (v - min) / (float)(max - min);
					}
				}
				proc_amp->Release();
			}
		}
		else
		{
			IAMCameraControl* camera_control = NULL;
			hr = it->second->QueryInterface(IID_PPV_ARGS(&camera_control));
			if (SUCCEEDED(hr))
			{
				long min, max, step, def, caps;
				hr = camera_control->GetRange(mf_prop, &min, &max, &step, &def, &caps);
				if (SUCCEEDED(hr))
				{
					long v = 0, f = 0;
					hr = camera_control->Get(mf_prop, &v, &f);
					if (SUCCEEDED(hr))
					{
						value = (v - min) / (float)(max - min);
					}
				}
				camera_control->Release();
			}
		}
		return value;
	}

	bool capture(const std::string& camera_id, OutputCameraData& output_data)
	{
		int i = find_camera_index(camera_id);
		if (i == -1)
		{
			return false;
		}
		auto it1 = m_mapSourceReader.find(i);
		if (it1 == m_mapSourceReader.end())
		{
			return false;
		}
		IMFSample* sample = NULL;
		DWORD stream_index = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		HRESULT hr = it1->second->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream_index, &flags, &timestamp, &sample);
		if (FAILED(hr))
		{
			return false;
		}
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			return false;
		}
		if (sample == NULL)
		{
			return false;
		}

		auto it2 = m_mapMediaType.find(i);
		if (it2 != m_mapMediaType.end())
		{
			UINT32 frame_width = 0;
			UINT32 frame_height = 0;
			MFGetAttributeSize(it2->second, MF_MT_FRAME_SIZE, &frame_width, &frame_height);
			output_data.width = frame_width;
			output_data.height = frame_height;
			GUID subtype;
			it2->second->GetGUID(MF_MT_SUBTYPE, &subtype);
			output_data.format = guid_to_camera_format(subtype);
			LONG stride = 0;
			MFGetStrideForBitmapInfoHeader(subtype.Data1, frame_width, &stride);
			output_data.stride = stride;
		}
		IMFMediaBuffer* buffer = NULL;
		hr = sample->ConvertToContiguousBuffer(&buffer);
		if (FAILED(hr))
		{
			sample->Release();
			return false;
		}
		buffer->GetCurrentLength(&output_data.size);
		if (output_data.data == nullptr)
		{
			output_data.data = new uint8_t[output_data.size];
		}
		uint8_t* data = nullptr;
		buffer->Lock(&data, nullptr, nullptr);
		if (output_data.stride < 0)
		{
			int stride = -output_data.stride;
			for (int i = output_data.height - 1; i >= 0; i--)
			{
				memcpy(output_data.data + (output_data.height - 1 - i) * stride, data + i * stride, stride);
			}
		}
		else
		{
			memcpy(output_data.data, data, output_data.size);
		}
		buffer->Unlock();
		buffer->Release();
		sample->Release();
		return true;
	}

private:
	CAMERA_COLOR_FORMAT guid_to_camera_format(GUID guid)
	{
		if (guid == MFVideoFormat_RGB32)
		{
			return CAMERA_RGB32;
		}
		else if (guid == MFVideoFormat_RGB24)
		{
			return CAMERA_RGB24;
		}
		else if (guid == MFVideoFormat_NV12)
		{
			return CAMERA_NV12;
		}
		else if (guid == MFVideoFormat_YUY2)
		{
			return CAMERA_YUY2;
		}
		else if (guid == MFVideoFormat_I420)
		{
			return CAMERA_I420;
		}
		else if (guid == MFVideoFormat_UYVY)
		{
			return CAMERA_UYVY;
		}
		return CAMERA_NONE;
	}

	int find_camera_index(const std::string& camera_id)
	{
		if (m_pMFActivates == nullptr)
		{
			return -1;
		}
		for (UINT32 i = 0; i < m_iCameraCount; i++)
		{
			WCHAR* guid = 0;
			UINT32 guid_len = 255;
			m_pMFActivates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &guid, &guid_len);
			std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
			if (camera_id == convert.to_bytes(guid))
			{
				return i;
			}
		}
		return -1;
	}

	int camera_prop_to_mf_prop(CAMERA_PROPETIES prop)
	{
		switch (prop)
		{
		case CAMERA_BRIGHTNESS:
			return VideoProcAmp_Brightness;
		case CAMERA_CONTRAST:
			return VideoProcAmp_Contrast;
		case CAMERA_HUE:
			return VideoProcAmp_Hue;
		case CAMERA_SATURATION:
			return VideoProcAmp_Saturation;
		case CAMERA_SHARPNESS:
			return VideoProcAmp_Sharpness;
		case CAMERA_GAMMA:
			return VideoProcAmp_Gamma;
		case CAMERA_COLORENABLE:
			return VideoProcAmp_ColorEnable;
		case CAMERA_WHITEBALANCE:
			return VideoProcAmp_WhiteBalance;
		case CAMERA_BACKLIGHTCOMPENSATION:
			return VideoProcAmp_BacklightCompensation;
		case CAMERA_GAIN:
			return VideoProcAmp_Gain;
		case CAMERA_PAN:
			return CameraControl_Pan;
		case CAMERA_TILT:
			return CameraControl_Tilt;
		case CAMERA_ROLL:
			return CameraControl_Roll;
		case CAMERA_ZOOM:
			return CameraControl_Zoom;
		case CAMERA_EXPOSURE:
			return CameraControl_Exposure;
		case CAMERA_IRIS:
			return CameraControl_Iris;
		case CAMERA_FOCUS:
			return CameraControl_Focus;
		default:
			return VideoProcAmp_Brightness;
		}
	}

	IMFAttributes* m_pAttributes{ nullptr };
	IMFActivate** m_pMFActivates{ nullptr };
	UINT32 m_iCameraCount{ 0 };
	std::map<int, IMFSourceReader*> m_mapSourceReader;
	std::map<int, IMFMediaSource*> m_mapMediaSource;
	std::map<int, IMFMediaType*> m_mapMediaType;

};

MFCameraCapture::MFCameraCapture()
{
	impl_ = new Impl();
}

MFCameraCapture::~MFCameraCapture()
{
	delete impl_;
}

int MFCameraCapture::get_camera_count()
{
	return impl_->get_camera_count();
}

std::string MFCameraCapture::get_camera_id(int index)
{
	return impl_->get_camera_id(index);
}

std::string MFCameraCapture::get_camera_name(int index)
{
	return impl_->get_camera_name(index);
}

bool MFCameraCapture::start(const std::string& camera_id, int& width, int& height, CAMERA_COLOR_FORMAT& format)
{
	return impl_->start(camera_id, width, height, format);
}

void MFCameraCapture::stop(const std::string& camera_id)
{
	impl_->stop(camera_id);
}

void MFCameraCapture::get_resolution_list(const std::string& camera_id, std::vector<std::pair<int, int>>& resolution_list)
{
	impl_->get_resolution_list(camera_id, resolution_list);
}

void MFCameraCapture::set_property(const std::string& camera_id, CAMERA_PROPETIES prop, float value, bool use_auto)
{
	impl_->set_property(camera_id, prop, value, use_auto);
}

float MFCameraCapture::get_property(const std::string& camera_id, CAMERA_PROPETIES prop)
{
	return impl_->get_property(camera_id, prop);
}

bool MFCameraCapture::capture(const std::string& camera_id, OutputCameraData& output_data)
{
	return impl_->capture(camera_id, output_data);
}