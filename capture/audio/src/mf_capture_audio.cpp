#include "mf_capture_audio.h"
#include "defer/defer.hpp"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <codecvt>
#include <map>

class MFAudioCapture::Impl
{
public:
	Impl()
	{
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		HRESULT hr = S_OK;
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
			(LPVOID*)&m_pDeviceEnumerator);
		if (FAILED(hr))
		{
			return;
		}
		hr = m_pDeviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &m_pMicCollection);
		if (FAILED(hr))
		{
			return;
		}
		hr = m_pDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &m_pSpeakerCollection);
		if (FAILED(hr))
		{
			return;
		}
	}

	~Impl()
	{
		if (m_pMicCollection)
		{
			m_pMicCollection->Release();
		}
		if (m_pSpeakerCollection)
		{
			m_pSpeakerCollection->Release();
		}
		if (m_pDeviceEnumerator)
		{
			m_pDeviceEnumerator->Release();
		}
		CoUninitialize();
	}

	int get_mic_count()
	{
		m_pMicCollection->Release();
		m_pDeviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &m_pMicCollection);
		UINT count = 0;
		m_pMicCollection->GetCount(&count);
		return count;
	}

	int get_speaker_count()
	{
		m_pSpeakerCollection->Release();
		m_pDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &m_pSpeakerCollection);
		UINT count = 0;
		m_pSpeakerCollection->GetCount(&count);
		return count;
	}

	std::string get_mic_id(int index)
	{
		IMMDevice* device = NULL;
		m_pMicCollection->Item(index, &device);
		if (device == nullptr)
		{
			return std::string();
		}
		wchar_t* deviceID = nullptr;
		device->GetId(&deviceID);
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
		std::string mic_id = convert.to_bytes(deviceID);
		size_t pos1 = mic_id.find(".{");
		size_t pos2 = mic_id.find_last_of('}');
		if (pos1 != std::string::npos && pos2 != std::string::npos)
		{
			mic_id = mic_id.substr(pos1 + 2, pos2 - pos1 - 2);
		}
		CoTaskMemFree(deviceID);
		device->Release();
		return mic_id;
	}

	std::string get_mic_name(int index)
	{
		IMMDevice* device = NULL;
		m_pMicCollection->Item(index, &device);
		if (device == nullptr)
		{
			return std::string();
		}
		std::string mic_name;
		IPropertyStore* pProps = NULL;
		device->OpenPropertyStore(STGM_READ, &pProps);
		if (pProps)
		{
			PROPVARIANT varName;
			PropVariantInit(&varName);
			pProps->GetValue(PKEY_Device_FriendlyName, &varName);
			std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
			mic_name = convert.to_bytes(varName.pwszVal);
			PropVariantClear(&varName);
			pProps->Release();
		}
		device->Release();
		return mic_name;
	}

	std::string get_speaker_id(int index)
	{
		IMMDevice* device = NULL;
		m_pSpeakerCollection->Item(index, &device);
		if (device == nullptr)
		{
			return std::string();
		}
		wchar_t* deviceID = nullptr;
		device->GetId(&deviceID);
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
		std::string speaker_id = convert.to_bytes(deviceID);
		size_t pos1 = speaker_id.find(".{");
		size_t pos2 = speaker_id.find_last_of('}');
		if (pos1 != std::string::npos && pos2 != std::string::npos)
		{
			speaker_id = speaker_id.substr(pos1 + 2, pos2 - pos1 - 2);
		}
		CoTaskMemFree(deviceID);
		device->Release();
		return speaker_id;
	}

	std::string get_speaker_name(int index)
	{
		IMMDevice* device = NULL;
		m_pSpeakerCollection->Item(index, &device);
		if (device == nullptr)
		{
			return std::string();
		}
		std::string speaker_name;
		IPropertyStore* pProps = NULL;
		device->OpenPropertyStore(STGM_READ, &pProps);
		if (pProps)
		{
			PROPVARIANT varName;
			PropVariantInit(&varName);
			pProps->GetValue(PKEY_Device_FriendlyName, &varName);
			std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
			speaker_name = convert.to_bytes(varName.pwszVal);
			PropVariantClear(&varName);
			pProps->Release();
		}
		device->Release();
		return speaker_name;
	}

	bool start_mic(const std::string& device_id)
	{
		if (m_pMicCollection == nullptr)
		{
			return false;
		}
		int i = find_device_index(m_pMicCollection, device_id);
		if (i == -1)
		{
			return false;
		}
		IMMDevice* device = NULL;
		m_pMicCollection->Item(i, &device);
		if (device == nullptr)
		{
			return false;
		}
		
		IAudioClient* pAudioClient = NULL;
		IAudioCaptureClient* pCaptureClient = NULL;
		HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
		device->Release();
		if (FAILED(hr))
		{
			return false;
		}
		WAVEFORMATEX* pwfx = NULL;
		hr = pAudioClient->GetMixFormat(&pwfx);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		AudioParam param;
		param.sample_rate = pwfx->nSamplesPerSec;
		param.channels = pwfx->nChannels;
		param.format = get_pcm_format(pwfx);
		m_mapMicParam[i] = param;
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pwfx, NULL);
		CoTaskMemFree(pwfx);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		hr = pAudioClient->Start();
		if (FAILED(hr))
		{
			pCaptureClient->Release();
			pAudioClient->Release();
			return false;
		}
		m_mapMicAudioClient[i] = pAudioClient;
		m_mapMicCaptureClient[i] = pCaptureClient;
		return true;
	}

	void stop_mic(const std::string& device_id)
	{
		int i = find_device_index(m_pMicCollection, device_id);
		if (i == -1)
		{
			return;
		}
		auto it1 = m_mapMicAudioClient.find(i);
		if (it1 != m_mapMicAudioClient.end())
		{
			it1->second->Stop();
			it1->second->Release();
			m_mapMicAudioClient.erase(it1);
		}
		auto it2 = m_mapMicCaptureClient.find(i);
		if (it2 != m_mapMicCaptureClient.end())
		{
			it2->second->Release();
			m_mapMicCaptureClient.erase(it2);
		}
		auto it3 = m_mapMicParam.find(i);
		if (it3 != m_mapMicParam.end())
		{
			m_mapMicParam.erase(it3);
		}
	}

	bool start_speaker(const std::string& device_id)
	{
		if (m_pSpeakerCollection == nullptr)
		{
			return false;
		}
		int i = find_device_index(m_pSpeakerCollection, device_id);
		if (i == -1)
		{
			return false;
		}
		IMMDevice* device = NULL;
		m_pSpeakerCollection->Item(i, &device);
		if (device == nullptr)
		{
			return false;
		}

		IAudioClient* pAudioClient = NULL;
		IAudioRenderClient* pRenderClient = NULL;
		HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
		device->Release();
		if (FAILED(hr))
		{
			return false;
		}
		WAVEFORMATEX* pwfx = NULL;
		hr = pAudioClient->GetMixFormat(&pwfx);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		AudioParam param;
		param.sample_rate = pwfx->nSamplesPerSec;
		param.channels = pwfx->nChannels;
		param.format = get_pcm_format(pwfx);
		m_mapSpeakerParam[i] = param;
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pwfx, NULL);
		CoTaskMemFree(pwfx);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
		if (FAILED(hr))
		{
			pAudioClient->Release();
			return false;
		}
		hr = pAudioClient->Start();
		if (FAILED(hr))
		{
			pRenderClient->Release();
			pAudioClient->Release();
			return false;
		}
		m_mapSpeakerAudioClient[i] = pAudioClient;
		m_mapSpeakerRenderClient[i] = pRenderClient;
		return true;
	}

	void stop_speaker(const std::string& device_id)
	{
		int i = find_device_index(m_pSpeakerCollection, device_id);
		if (i == -1)
		{
			return;
		}
		auto it1 = m_mapSpeakerAudioClient.find(i);
		if (it1 != m_mapSpeakerAudioClient.end())
		{
			it1->second->Stop();
			it1->second->Release();
			m_mapSpeakerAudioClient.erase(it1);
		}
		auto it2 = m_mapSpeakerRenderClient.find(i);
		if (it2 != m_mapSpeakerRenderClient.end())
		{
			it2->second->Release();
			m_mapSpeakerRenderClient.erase(it2);
		}
		auto it3 = m_mapSpeakerParam.find(i);
		if (it3 != m_mapSpeakerParam.end())
		{
			m_mapSpeakerParam.erase(it3);
		}
	}

	bool capture_mic(const std::string& device_id, OutputAudioData& output_data)
	{
		int i = find_device_index(m_pMicCollection, device_id);
		if (i == -1)
		{
			return false;
		}
		auto it = m_mapMicCaptureClient.find(i);
		if (it == m_mapMicCaptureClient.end())
		{
			return false;
		}
		BYTE* pData = NULL;
		UINT32 numFramesAvailable = 0;
		DWORD flags = 0;
		HRESULT hr = it->second->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
		if (FAILED(hr))
		{
			return false;
		}
		output_data.data = pData;
		output_data.param = m_mapMicParam[i];
		output_data.samples = numFramesAvailable;
		return true;
	}

	bool capture_speaker(const std::string& device_id, OutputAudioData& output_data)
	{
		int i = find_device_index(m_pSpeakerCollection, device_id);
		if (i == -1)
		{
			return false;
		}
		auto it = m_mapSpeakerRenderClient.find(i);
		if (it == m_mapSpeakerRenderClient.end())
		{
			return false;
		}
		BYTE* pData = NULL;
		UINT32 numFramesPadding = 0;
		HRESULT hr = it->second->GetBuffer(output_data.samples, &pData);
		if (FAILED(hr))
		{
			return false;
		}
		output_data.data = pData;
		output_data.param = m_mapSpeakerParam[i];
		return true;
	}

private:
	int find_device_index(IMMDeviceCollection* collection, const std::string& device_id)
	{
		UINT count = 0;
		collection->GetCount(&count);
		for (UINT i = 0; i < count; i++)
		{
			IMMDevice* device = NULL;
			collection->Item(i, &device);
			if (device == nullptr)
			{
				continue;
			}
			wchar_t* deviceID = nullptr;
			device->GetId(&deviceID);
			std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
			std::string id = convert.to_bytes(deviceID);
			size_t pos1 = id.find(".{");
			size_t pos2 = id.find_last_of('}');
			if (pos1 != std::string::npos && pos2 != std::string::npos)
			{
				id = id.substr(pos1 + 2, pos2 - pos1 - 2);
			}
			CoTaskMemFree(deviceID);
			device->Release();
			if (id == device_id)
			{
				return i;
			}
		}
		return -1;
	}

	PCM_FORMAT get_pcm_format(WAVEFORMATEX* pwfx)
	{
		if (pwfx->wFormatTag == WAVE_FORMAT_PCM)
		{
			if (pwfx->wBitsPerSample == 8)
			{
				return PCM_U8;
			}
			else if (pwfx->wBitsPerSample == 16)
			{
				return PCM_S16;
			}
			else if (pwfx->wBitsPerSample == 32)
			{
				return PCM_S32;
			}
			else if (pwfx->wBitsPerSample == 64)
			{
				return PCM_S64;
			}
		}
		else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
		{
			if (pwfx->wBitsPerSample == 32)
			{
				return PCM_FLT;
			}
			else if (pwfx->wBitsPerSample == 64)
			{
				return PCM_DBL;
			}
		}
		else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			WAVEFORMATEXTENSIBLE* pEx = (WAVEFORMATEXTENSIBLE*)pwfx;
			if (pEx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
			{
				if (pwfx->wBitsPerSample == 8)
				{
					return PCM_U8P;
				}
				else if (pwfx->wBitsPerSample == 16)
				{
					return PCM_S16P;
				}
				else if (pwfx->wBitsPerSample == 32)
				{
					return PCM_S32P;
				}
				else if (pwfx->wBitsPerSample == 64)
				{
					return PCM_S64P;
				}
			}
			else if (pEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				if (pwfx->wBitsPerSample == 32)
				{
					return PCM_FLTP;
				}
				else if (pwfx->wBitsPerSample == 64)
				{
					return PCM_DBLP;
				}
			}
		}
		return PCM_UNKNOWN;
	}

	IMMDeviceEnumerator* m_pDeviceEnumerator { nullptr };
	IMMDeviceCollection* m_pMicCollection { nullptr };
	IMMDeviceCollection* m_pSpeakerCollection { nullptr };
	std::map<int, IAudioClient*> m_mapMicAudioClient;
	std::map<int, IAudioCaptureClient*> m_mapMicCaptureClient;
	std::map<int, IAudioClient*> m_mapSpeakerAudioClient;
	std::map<int, IAudioRenderClient*> m_mapSpeakerRenderClient;
	std::map<int, AudioParam> m_mapMicParam;
	std::map<int, AudioParam> m_mapSpeakerParam;
};

MFAudioCapture::MFAudioCapture()
{
	impl_ = new Impl();
}

MFAudioCapture::~MFAudioCapture()
{
	delete impl_;
}

int MFAudioCapture::get_mic_count()
{
	return impl_->get_mic_count();
}

int MFAudioCapture::get_speaker_count()
{
	return impl_->get_speaker_count();
}

std::string MFAudioCapture::get_mic_id(int index)
{
	return impl_->get_mic_id(index);
}

std::string MFAudioCapture::get_mic_name(int index)
{
	return impl_->get_mic_name(index);
}

std::string MFAudioCapture::get_speaker_id(int index)
{
	return impl_->get_speaker_id(index);
}

std::string MFAudioCapture::get_speaker_name(int index)
{
	return impl_->get_speaker_name(index);
}

bool MFAudioCapture::start_mic(const std::string& device_id)
{
	return impl_->start_mic(device_id);
}

void MFAudioCapture::stop_mic(const std::string& device_id)
{
	impl_->stop_mic(device_id);
}

bool MFAudioCapture::start_speaker(const std::string& device_id)
{
	return impl_->start_speaker(device_id);
}

void MFAudioCapture::stop_speaker(const std::string& device_id)
{
	impl_->stop_speaker(device_id);
}

bool MFAudioCapture::capture_mic(const std::string& device_id, OutputAudioData& output_data)
{
	return impl_->capture_mic(device_id, output_data);
}

bool MFAudioCapture::capture_speaker(const std::string& device_id, OutputAudioData& output_data)
{
	return impl_->capture_speaker(device_id, output_data);
}