#ifndef MF_CAPTURE_AUDIO_H
#define MF_CAPTURE_AUDIO_H

#include <string>
#include <vector>

enum PCM_FORMAT
{
    PCM_UNKNOWN = -1,
    PCM_U8 = 0,
    PCM_S16,
    PCM_S32,
    PCM_FLT,
    PCM_DBL,
    PCM_U8P,
    PCM_S16P,
    PCM_S32P,
    PCM_FLTP,
    PCM_DBLP,
    PCM_S64,
    PCM_S64P
};

struct AudioParam
{
	int sample_rate;
	int channels;
	PCM_FORMAT format;
};

struct OutputAudioData
{
	uint8_t* data;
    int samples;
    AudioParam param;
};

class __declspec(dllexport) MFAudioCapture final
{
public:
    MFAudioCapture();
    ~MFAudioCapture();

    int get_mic_count();
    int get_speaker_count();
    std::string get_mic_id(int index);
    std::string get_mic_name(int index);
    std::string get_speaker_id(int index);
    std::string get_speaker_name(int index);
    bool start_mic(const std::string& device_id);
    void stop_mic(const std::string& device_id);
    bool start_speaker(const std::string& device_id);
    void stop_speaker(const std::string& device_id);

    bool capture_mic(const std::string& device_id, OutputAudioData& output_data);
    bool capture_speaker(const std::string& device_id, OutputAudioData& output_data);

private:
    class Impl;
    Impl* impl_;
};
#endif