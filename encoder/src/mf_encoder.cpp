
#include "mf_encoder.h"
#include "defer/defer.hpp"
#include "dx11convert/dx11convert.h"
#include "libyuv/include/libyuv.h"
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")

#define MPEG_TIME_BASE 90000
#define XALIGN(x, a) (((x) + (a)-1) & ~((a)-1))

struct CropRect
{
	float left;
	float top;
	float right;
	float bottom;
};

class MFVideoEncoder::Impl
{
public:
    Impl(ID3D11Device* d3d_device, ID3D11DeviceContext* d3d_context)
        : m_pD3DDevice(d3d_device)
        , m_pD3DDeviceCtx(d3d_context)
    {
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		MFStartup(MF_VERSION);
        if (m_pD3DDevice == nullptr && m_pD3DDeviceCtx == nullptr)
        {
            D3D_FEATURE_LEVEL featureLevels[] =
            {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0
            };

            UINT uiFeatureLevels = ARRAYSIZE(featureLevels);
            D3D_FEATURE_LEVEL featureLevel;
            UINT uiD3D11CreateFlag = D3D11_CREATE_DEVICE_SINGLETHREADED;
            D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, uiD3D11CreateFlag, featureLevels, uiFeatureLevels, D3D11_SDK_VERSION, &m_pD3DDevice, &featureLevel, &m_pD3DDeviceCtx);
            m_bOwnD3DDevice = true;
        }
	}

    ~Impl()
    {
		MFShutdown();
		CoUninitialize();
	}
	bool start(int width, int height, float fps)
	{
        bool ret = true;
        IMFMediaType* pInputType = nullptr;
        IMFMediaType* pOutputType = nullptr;
        defer[&]{
            if (pInputType)
            {
                pInputType->Release();
            }
            if (pOutputType)
            {
                pOutputType->Release();
            }
            if (!ret)
            {
                if (m_pMFTEncoder)
                {
                    m_pMFTEncoder->Release();
                    m_pMFTEncoder = nullptr;
                }
                if (m_pMFTConvert)
                {
                    m_pMFTConvert->Release();
                    m_pMFTConvert = nullptr;
                }
            }
        };

        UINT32 frame_width = XALIGN((UINT32)(width * (m_tCropRatio.right - m_tCropRatio.left)), 16);
        UINT32 frame_height = XALIGN((UINT32)(height * (m_tCropRatio.bottom - m_tCropRatio.top)), 2);
        m_iEncodedWidth = XALIGN((UINT32)(frame_width * m_fScaleRatio), 16);
        m_iEncodedHeight = XALIGN((UINT32)(frame_height * m_fScaleRatio), 2);
        HRESULT hr = CoCreateInstance(CLSID_MSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pMFTEncoder));
        if (FAILED(hr))
        {
            ret = false;
            return ret;
        }
        int fps_num = (int)(fps * 1000);
        int fps_den = 1000;
        MFCreateMediaType(&pOutputType);
        pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        MFSetAttributeSize(pOutputType, MF_MT_FRAME_SIZE, m_iEncodedWidth, m_iEncodedHeight);
        MFSetAttributeRatio(pOutputType, MF_MT_FRAME_RATE, fps_num, fps_den);
        pOutputType->SetUINT32(MF_MT_MAX_KEYFRAME_SPACING, fps_num * 5 / fps_den);
        pOutputType->SetUINT32(MF_MT_AVG_BITRATE, m_iEncodedWidth * m_iEncodedHeight * 100);
        pOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        pOutputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, FALSE);
        hr = m_pMFTEncoder->SetOutputType(0, pOutputType, 0);
        if (FAILED(hr))
        {
            ret = false;
            return ret;
        }
        MFCreateMediaType(&pInputType);
        pInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(pInputType, MF_MT_FRAME_SIZE, m_iEncodedWidth, m_iEncodedHeight);
        MFSetAttributeRatio(pInputType, MF_MT_FRAME_RATE, fps_num, fps_den);
        MFSetAttributeRatio(pInputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = m_pMFTEncoder->SetInputType(0, pInputType, 0);
        if (FAILED(hr))
        {
            pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_IYUV);
            hr = m_pMFTEncoder->SetInputType(0, pInputType, 0);
            if (FAILED(hr))
            {
                ret = false;
                return ret;
            }
            hr = CoCreateInstance(CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pMFTConvert));
            if (FAILED(hr))
            {
                ret = false;
                return ret;
            }
            hr = m_pMFTConvert->SetOutputType(0, pInputType, 0);
            if (FAILED(hr))
            {
                ret = false;
                return ret;
            }
            MFSetAttributeSize(pInputType, MF_MT_FRAME_SIZE, frame_width, frame_height);
            pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            hr = m_pMFTConvert->SetInputType(0, pInputType, 0);
            if (FAILED(hr))
            {
                ret = false;
                return ret;
            }
        }
        else
        {
            m_pDX11ShaderNV12 = new DX11ShaderNV12(m_pD3DDevice, m_pD3DDeviceCtx);
            m_pDX11ShaderNV12->prepare_resources(m_iEncodedWidth, m_iEncodedHeight);
        }
        return ret;
	}

	void stop()
	{
        if (m_pMFTEncoder)
        {
            m_pMFTEncoder->Release();
            m_pMFTEncoder = nullptr;
        }
        if (m_pMFTConvert)
        {
            m_pMFTConvert->Release();
            m_pMFTConvert = nullptr;
        }
        if (m_pDX11ShaderNV12)
        {
            m_pDX11ShaderNV12->release_resources();
            delete m_pDX11ShaderNV12;
            m_pDX11ShaderNV12 = nullptr;
        }
        if (m_bOwnD3DDevice)
        {
            m_pD3DDeviceCtx->Release();
            m_pD3DDeviceCtx = nullptr;
			m_pD3DDevice->Release();
			m_pD3DDevice = nullptr;
		}
        m_iFrameCount = 0;
        m_tCropRatio = { 0.0f, 0.0f, 1.0f, 1.0f };
        m_fScaleRatio = 1.0f;
	}

    void set_time_base(int64_t time_base)
    {
        m_iTimeBase = time_base;
    }

    void set_crop_rect(float left, float top, float right, float bottom)
    {
        m_tCropRatio.left = left;
        m_tCropRatio.top = top;
        m_tCropRatio.right = right;
        m_tCropRatio.bottom = bottom;
    }

    void set_scale_ratio(float ratio)
    {
        m_fScaleRatio = ratio;
	}

	int encode(const InputVTextureData& input_data, OutputVData& output_data)
	{
        UINT32 width = 0;
        UINT32 height = 0;
        VIDEO_FORMAT format = VIDEO_FORMAT_IYUV;
        float fps = 0.0f;
        if (!get_encoder_config(width, height, format, fps))
        {
            return ENCODE_FAIL;
        }

        IMFSample* yuv_sample = nullptr;
        defer[&]{
            if (yuv_sample)
            {
                yuv_sample->Release();
            }
        };
        if (input_data.texture)
        {
            ID3D11Texture2D* cropped_texture = nullptr;
            if (abs(m_tCropRatio.right - m_tCropRatio.left - 1.0f) > 0.01f || abs(m_tCropRatio.bottom - m_tCropRatio.top - 1.0f) > 0.01f)
            {
                crop_texture(input_data.texture, &cropped_texture);
            }
            else
            {
                cropped_texture = input_data.texture;
            }
            if (format != input_data.format)
            {
                yuv_sample = get_yuv_texture_sample(cropped_texture);
            }
            else
            {
                IMFMediaBuffer* input_buffer = nullptr;
                HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), cropped_texture, 0, FALSE, &input_buffer);
                if (FAILED(hr))
                {
                    return ENCODE_FAIL;
                }
                MFCreateSample(&yuv_sample);
                yuv_sample->AddBuffer(input_buffer);
                input_buffer->Release();
            }
            if (cropped_texture != input_data.texture)
            {
                cropped_texture->Release();
            }
        }
        if (yuv_sample)
        {
            int64_t frame_duration = (int64_t)(m_iTimeBase / fps);
            yuv_sample->SetSampleDuration(frame_duration);
            yuv_sample->SetSampleTime(m_iFrameCount * frame_duration);
            m_iFrameCount++;
            yuv_sample->SetUINT32(MFSampleExtension_VideoEncodeQP, 10);
        }
        return internal_encode(yuv_sample, output_data);
	}

	int encode(const InputVMemoryData& input_data, OutputVData& output_data)
	{
        UINT32 width = 0;
        UINT32 height = 0;
        VIDEO_FORMAT format = VIDEO_FORMAT_IYUV;
        float fps = 0.0f;
        if (!get_encoder_config(width, height, format, fps))
        {
            return ENCODE_FAIL;
        }

        IMFSample* yuv_sample = nullptr;
        if (input_data.data)
        {
            uint8_t* cropped_data = nullptr;
            if (abs(m_tCropRatio.right - m_tCropRatio.left - 1.0f) > 0.01f || abs(m_tCropRatio.bottom - m_tCropRatio.top - 1.0f) > 0.01f)
            {
                crop_memory_data(input_data.data, &cropped_data, input_data.width, input_data.height, input_data.format);
            }
            else
            {
                cropped_data = input_data.data;
            }
            IMFMediaBuffer* input_buffer = nullptr;
            uint8_t* data = nullptr;
            if (format != input_data.format)
            {
                MFCreateMemoryBuffer(width * height * 3 / 2, &input_buffer);
                input_buffer->Lock(&data, nullptr, nullptr);
                if (input_data.format == VIDEO_FORMAT_RGB32)
                {
                    if (format == VIDEO_FORMAT_NV12)
                    {
                        libyuv::ARGBToNV12(cropped_data, width * 4, data, width, data + width * height, width, width, height);
                    }
                    else if (format == VIDEO_FORMAT_IYUV)
                    {
                        libyuv::ARGBToI420(cropped_data, width * 4, data, width, data + width * height, width / 2, data + width * height * 5 / 4, width / 2, width, height);
                    }
                }
                else if (input_data.format == VIDEO_FORMAT_NV12)
                {
                    if (format == VIDEO_FORMAT_IYUV)
                    {
						libyuv::NV12ToI420(cropped_data, width, cropped_data + width * height, width, data, width, data + width * height, width / 2, data + width * height * 5 / 4, width / 2, width, height);
					}
				}
                else if (input_data.format == VIDEO_FORMAT_IYUV)
                {
                    if (format == VIDEO_FORMAT_NV12)
                    {
						libyuv::I420ToNV12(cropped_data, width, cropped_data + width * height, width / 2, cropped_data + width * height * 5 / 4, width / 2, data, width, data + width * height, width, width, height);
					}
				}
            }
            else
            {
                MFCreateMemoryBuffer(input_data.size, &input_buffer);
                input_buffer->Lock(&data, nullptr, nullptr);
                if (input_data.format == VIDEO_FORMAT_RGB32)
                {
					memcpy(data, cropped_data, width * height * 4);
				}
				else
				{
					memcpy(data, cropped_data, width * height * 3 / 2);
				}
            }
            input_buffer->Unlock();
            MFCreateSample(&yuv_sample);
            yuv_sample->AddBuffer(input_buffer);
            input_buffer->Release();
            if (cropped_data != input_data.data)
            {
                delete[] cropped_data;
            }
        }

        if (yuv_sample)
        {
            int64_t frame_duration = (int64_t)(m_iTimeBase / fps);
            yuv_sample->SetSampleDuration(frame_duration);
            yuv_sample->SetSampleTime(m_iFrameCount * frame_duration);
            m_iFrameCount++;
            yuv_sample->SetUINT32(MFSampleExtension_VideoEncodeQP, 10);
        }
        return internal_encode(yuv_sample, output_data);
	}

private:
    IMFSample* get_yuv_texture_sample(ID3D11Texture2D* input_texture)
    {
        IMFMediaBuffer* input_buffer = nullptr;
        IMFMediaBuffer* output_buffer = nullptr;
        IMFSample* bgra_sample = nullptr;
        IMFSample* yuv_sample = nullptr;

        defer[&]{
            if (input_buffer)
            {
                input_buffer->Release();
            }
            if (output_buffer)
            {
                output_buffer->Release();
            }
            if (bgra_sample)
            {
                bgra_sample->RemoveAllBuffers();
                bgra_sample->Release();
            }
        };

        if (m_pDX11ShaderNV12)
        {
            ID3D11Texture2D* nv12_texture = nullptr;
            ID3D11Texture2D* output_texture = nullptr;
            m_pDX11ShaderNV12->process_shader_nv12(input_texture, &nv12_texture);
            D3D11_TEXTURE2D_DESC desc = {};
            nv12_texture->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            HRESULT hr1 = m_pD3DDevice->CreateTexture2D(&desc, nullptr, &output_texture);
            if (FAILED(hr1))
            {
				return nullptr;
			}
            m_pD3DDeviceCtx->CopyResource(output_texture, nv12_texture);
            nv12_texture->Release();
            m_pDX11ShaderNV12->release_input_texture();
            MFCreateSample(&yuv_sample);
            MFCreateMemoryBuffer(desc.Width * desc.Height, &input_buffer);
            D3D11_MAPPED_SUBRESOURCE mapped_resource;
            m_pD3DDeviceCtx->Map(output_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
            uint8_t* data = nullptr;
            input_buffer->Lock(&data, nullptr, nullptr);
            memcpy(data, mapped_resource.pData, desc.Width * desc.Height);
            input_buffer->Unlock();
            m_pD3DDeviceCtx->Unmap(output_texture, 0);
            output_texture->Release();
            yuv_sample->AddBuffer(input_buffer);
        }
        else
        {
            if (m_pMFTConvert)
            {
                HRESULT hr1 =
                    MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), input_texture, 0, FALSE, &input_buffer);
                if (FAILED(hr1))
                {
                    return nullptr;
                }
                MFCreateSample(&bgra_sample);
                bgra_sample->AddBuffer(input_buffer);
                hr1 = m_pMFTConvert->ProcessInput(0, bgra_sample, 0);
                if (FAILED(hr1) && hr1 != 0xC00D36B5)
                {
                    return nullptr;
                }
                MFT_OUTPUT_DATA_BUFFER output_data = {};
                output_data.dwStreamID = 0;
                output_data.dwStatus = 0;
                output_data.pSample = nullptr;
                MFCreateSample(&output_data.pSample);
                MFCreateMemoryBuffer(m_iEncodedWidth * m_iEncodedHeight * 3 / 2, &output_buffer);
                output_data.pSample->AddBuffer(output_buffer);
                DWORD dwStatus = 0;
                HRESULT hr2 = m_pMFTConvert->ProcessOutput(0, 1, &output_data, &dwStatus);
                if (hr2 == 0xC00D6D72 && hr1 == 0xC00D36B5)
                {
                    hr1 = m_pMFTConvert->ProcessInput(0, bgra_sample, 0);
                    hr2 = m_pMFTConvert->ProcessOutput(0, 1, &output_data, &dwStatus);
                }
                if (FAILED(hr2))
                {
                    return nullptr;
                }
                yuv_sample = output_data.pSample;
            }
        }
        return yuv_sample;
    }

    GUID video_format_to_guid(VIDEO_FORMAT format)
    {
        switch (format)
        {
        case VIDEO_FORMAT_IYUV:
            return MFVideoFormat_IYUV;
        case VIDEO_FORMAT_NV12:
	        return MFVideoFormat_NV12;
        case VIDEO_FORMAT_YV12:
	        return MFVideoFormat_YV12;
        case VIDEO_FORMAT_RGB32:
	        return MFVideoFormat_RGB32;
        default:
	        return GUID_NULL;
        }
	}

    VIDEO_FORMAT guid_to_video_format(GUID guid)
    {
        if (guid == MFVideoFormat_IYUV)
        {
            return VIDEO_FORMAT_IYUV;
        }
        else if (guid == MFVideoFormat_NV12)
        {
	        return VIDEO_FORMAT_NV12;
        }
        else if (guid == MFVideoFormat_YV12)
        {
	        return VIDEO_FORMAT_YV12;
        }
        else if (guid == MFVideoFormat_RGB32)
        {
	        return VIDEO_FORMAT_RGB32;
        }
        else
        {
	        return VIDEO_FORMAT_MAX;
        }
	}

    bool get_encoder_config(UINT32& width, UINT32& height, VIDEO_FORMAT& format, float& fps)
    {
        IMFMediaType* input_type = nullptr;
        m_pMFTEncoder->GetInputCurrentType(0, &input_type);
        if (input_type == nullptr)
        {
            return false;
        }
        MFGetAttributeSize(input_type, MF_MT_FRAME_SIZE, &width, &height);
        UINT32 fps_num = 0;
        UINT32 fps_den = 0;
        MFGetAttributeRatio(input_type, MF_MT_FRAME_RATE, &fps_num, &fps_den);
        fps = (float)fps_num / fps_den;
        GUID guid_format;
        input_type->GetGUID(MF_MT_SUBTYPE, &guid_format);
        format = guid_to_video_format(guid_format);
        input_type->Release();
        return true;
    }

    int internal_encode(IMFSample* yuv_sample, OutputVData& output_data)
    {
        HRESULT hr1 = S_OK;
        HRESULT hr2 = S_OK;
        IMFMediaBuffer* output_buffer = nullptr;
        MFCreateMemoryBuffer(16 * 1024 * 1024, &output_buffer);
        if (output_data.data == nullptr)
        {
            output_data.data = new uint8_t[16 * 1024 * 1024];
        }

        do
        {
            if (yuv_sample)
            {
                hr1 = m_pMFTEncoder->ProcessInput(0, yuv_sample, 0);
                if (FAILED(hr1) && hr1 != 0xC00D36B5)
                {
                    output_buffer->Release();
                    return ENCODE_FAIL;
                }
            }
            else
            {
                m_pMFTEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                m_pMFTEncoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            }
            MFT_OUTPUT_DATA_BUFFER mft_output_data = {};
            mft_output_data.dwStreamID = 0;
            mft_output_data.dwStatus = 0;
            mft_output_data.pSample = nullptr;
            DWORD dwStatus = 0;
            MFCreateSample(&mft_output_data.pSample);
            mft_output_data.pSample->AddBuffer(output_buffer);
            hr2 = m_pMFTEncoder->ProcessOutput(0, 1, &mft_output_data, &dwStatus);
            if (FAILED(hr2) && hr2 != 0xC00D6D72)
            {
                mft_output_data.pSample->Release();
                output_buffer->Release();
                return ENCODE_FAIL;
            }
            if (SUCCEEDED(hr2))
            {
                mft_output_data.pSample->GetSampleDuration(&output_data.duration);
                mft_output_data.pSample->GetSampleTime(&output_data.timestamp);
                UINT32 key_frame = 0;
                mft_output_data.pSample->GetUINT32(MFSampleExtension_CleanPoint, &key_frame);
                output_data.key_frame = key_frame != 0;
                output_buffer->GetCurrentLength(&output_data.size);
                uint8_t* data = nullptr;
                output_buffer->Lock(&data, nullptr, nullptr);
                memcpy(output_data.data, data, output_data.size);
                output_buffer->Unlock();
            }
            mft_output_data.pSample->Release();
        } while (hr1 == 0xC00D36B5);
        output_buffer->Release();
        if (hr2 == 0xC00D6D72)
        {
            if (yuv_sample == nullptr)
            {
                return ENCODE_EOF;
            }
            else
            {
                return ENCODE_MORE_INPUT;
            }
        }
        return ENCODE_SUCCESS;
    }

    void crop_texture(ID3D11Texture2D* input_texture, ID3D11Texture2D** output_texture)
    {
        D3D11_TEXTURE2D_DESC input_desc = {};
        input_texture->GetDesc(&input_desc);
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = XALIGN((UINT)(input_desc.Width * (m_tCropRatio.right - m_tCropRatio.left)), 16);
        desc.Height = XALIGN((UINT)(input_desc.Height * (m_tCropRatio.bottom - m_tCropRatio.top)), 2);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = input_desc.Format;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        HRESULT hr = m_pD3DDevice->CreateTexture2D(&desc, nullptr, output_texture);
        if (SUCCEEDED(hr))
        {
            UINT left = (UINT)(input_desc.Width * m_tCropRatio.left);
            UINT top = (UINT)(input_desc.Height * m_tCropRatio.top);
            D3D11_BOX box = { left, top, 0, left + desc.Width, top + desc.Height, 1 };
            m_pD3DDeviceCtx->CopySubresourceRegion(*output_texture, 0, 0, 0, 0, input_texture, 0, &box);
        }
    }

    void crop_memory_data(uint8_t* input_data, uint8_t** output_data, UINT width, UINT height, VIDEO_FORMAT format)
	{
		UINT frame_width = XALIGN((UINT)(width * (m_tCropRatio.right - m_tCropRatio.left)), 16);
		UINT frame_height = XALIGN((UINT)(height * (m_tCropRatio.bottom - m_tCropRatio.top)), 2);
        if (format == VIDEO_FORMAT_RGB32)
        {
            *output_data = new uint8_t[frame_width * frame_height * 4];
            for (UINT i = 0; i < frame_height; i++)
            {
                memcpy(*output_data + i * frame_width * 4, input_data + (UINT)(i + height * m_tCropRatio.top) * width * 4 + (UINT)(width * m_tCropRatio.left) * 4, frame_width * 4);
            }
        }
        else if (format == VIDEO_FORMAT_NV12)
        {
            *output_data = new uint8_t[frame_width * frame_height * 3 / 2];
            for (UINT i = 0; i < frame_height; i++)
			{
				memcpy(*output_data + i * frame_width, input_data + (UINT)(i + height * m_tCropRatio.top) * width + (UINT)(width * m_tCropRatio.left), frame_width);
			}
            for (UINT i = 0; i < frame_height / 2; i++)
            {
                memcpy(*output_data + frame_width * frame_height + i * frame_width, input_data + width * height + (UINT)(i + height * m_tCropRatio.top) * width + (UINT)(width * m_tCropRatio.left), frame_width);
            }
        }
        else if (format == VIDEO_FORMAT_IYUV)
        {
            *output_data = new uint8_t[frame_width * frame_height * 3 / 2];
            for (UINT i = 0; i < frame_height; i++)
            {
                memcpy(*output_data + i * frame_width, input_data + (UINT)(i + height * m_tCropRatio.top) * width + (UINT)(width * m_tCropRatio.left), frame_width);
            }
            for (UINT i = 0; i < frame_height / 2; i++)
            {
                memcpy(*output_data + frame_width * frame_height + i * frame_width / 2, input_data + width * height + (UINT)(i + height * m_tCropRatio.top) * width / 2 + (UINT)(width * m_tCropRatio.left) / 2, frame_width / 2);
            }
            for (UINT i = 0; i < frame_height / 2; i++)
            {
                memcpy(*output_data + frame_width * frame_height * 5 / 4 + i * frame_width / 2, input_data + width * height * 5 / 4 + (UINT)(i + height * m_tCropRatio.top) * width / 2 + (UINT)(width * m_tCropRatio.left) / 2, frame_width / 2);
            }
        }
	}

    ID3D11Device* m_pD3DDevice{ nullptr };
    ID3D11DeviceContext* m_pD3DDeviceCtx{ nullptr };
    IMFTransform* m_pMFTEncoder{ nullptr };
    IMFTransform* m_pMFTConvert{ nullptr };
    DX11ShaderNV12* m_pDX11ShaderNV12{ nullptr };
    bool m_bOwnD3DDevice{ false };

    int64_t m_iTimeBase{ MPEG_TIME_BASE };
    int64_t m_iFrameCount{ 0 };
    UINT32 m_iEncodedWidth{ 0 };
    UINT32 m_iEncodedHeight{ 0 };

    CropRect m_tCropRatio{ 0.0f, 0.0f, 1.0f, 1.0f };
    float m_fScaleRatio{ 1.0f };
};




MFVideoEncoder::MFVideoEncoder(ID3D11Device* d3d_device, ID3D11DeviceContext* d3d_context)
{
	impl_ = new Impl(d3d_device, d3d_context);
}

MFVideoEncoder::~MFVideoEncoder()
{
	delete impl_;
}

bool MFVideoEncoder::start(int width, int height, float fps)
{
    return impl_->start(width, height, fps);
}

void MFVideoEncoder::stop()
{
	impl_->stop();
}

void MFVideoEncoder::set_time_base(int64_t time_base)
{
    impl_->set_time_base(time_base);
}

void MFVideoEncoder::set_crop_rect(float left, float top, float right, float bottom)
{
    impl_->set_crop_rect(left, top, right, bottom);
}

void MFVideoEncoder::set_scale_ratio(float ratio)
{
    impl_->set_scale_ratio(ratio);
}

int MFVideoEncoder::encode(const InputVTextureData& input_data, OutputVData& output_data)
{
	return impl_->encode(input_data, output_data);
}

int MFVideoEncoder::encode(const InputVMemoryData& input_data, OutputVData& output_data)
{
	return impl_->encode(input_data, output_data);
}