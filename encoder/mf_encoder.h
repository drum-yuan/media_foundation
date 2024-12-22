#ifndef MF_ENCODER_H
#define MF_ENCODER_H

#include <d3d11.h>
#include <stdint.h>

enum ERROR_CODE
{
    ENCODE_SUCCESS = 0,
    ENCODE_FAIL,
    ENCODE_MORE_INPUT,
    ENCODE_EOF
};

enum VIDEO_FORMAT
{
    VIDEO_FORMAT_IYUV = 0,
	VIDEO_FORMAT_NV12,
    VIDEO_FORMAT_YV12,
	VIDEO_FORMAT_RGB32,
	VIDEO_FORMAT_MAX
};

enum AUDIO_FORMAT
{
	AUDIO_FORMAT_U8 = 0,
	AUDIO_FORMAT_S16LE,
	AUDIO_FORMAT_S24LE,
	AUDIO_FORMAT_S32LE,
	AUDIO_FORMAT_FLT,
	AUDIO_FORMAT_DBL
};

struct InputVMemoryData
{
    int width;
    int height;
    VIDEO_FORMAT format;
    uint8_t* data;
    unsigned long size;
};

struct InputVTextureData
{
    int width;
    int height;
    VIDEO_FORMAT format;
    ID3D11Texture2D* texture;
};

struct OutputVData
{
    uint8_t* data;
    unsigned long size;
    int64_t duration;
    int64_t timestamp;
    bool key_frame;
};

struct InputAMemoryData
{
	int sample_rate;
	int channels;
	int format;
	uint8_t* data;
	unsigned long size;
};

struct OutputAData
{
	uint8_t* data;
	unsigned long size;
    int64_t duration;
	int64_t timestamp;
};

class __declspec(dllexport) MFVideoEncoder final
{
public:
    MFVideoEncoder(ID3D11Device* d3d_device, ID3D11DeviceContext* d3d_context);
    ~MFVideoEncoder();

    bool start(int width, int height, float fps); // width and height must be consistent with input data
    void stop();
    void set_time_base(int64_t time_base); // if not set, default is 90000. otherwise output timestamp is invalid
    void set_crop_rect(float left, float top, float right, float bottom); // if not set, default is 0.0f, 0.0f, 1.0f, 1.0f
    void set_scale_ratio(float ratio); // if not set, default is 1.0f

    int encode(const InputVTextureData& input_data, OutputVData& output_data);
    int encode(const InputVMemoryData& input_data, OutputVData& output_data);

private:
    class Impl;
    Impl* impl_;
};

class __declspec(dllexport) MFAudioEncoder final
{
public:
	MFAudioEncoder();
	~MFAudioEncoder();

	bool start(int sample_rate, int channels, AUDIO_FORMAT format);
	void stop();
	void set_time_base(int64_t time_base); // if not set, default is 90000. otherwise output timestamp is invalid

	int encode(const InputAMemoryData& input_data, OutputAData& output_data);

private:
    class Impl;
    Impl* impl_;
};

#endif