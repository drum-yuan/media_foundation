#ifndef MF_CAMERA_CAMERA_H
#define MF_CAMERA_CAMERA_H

#include <string>
#include <vector>

enum CAMERA_COLOR_FORMAT
{
	CAMERA_NONE = 0,
	CAMERA_RGB32,
	CAMERA_RGB24,
	CAMERA_NV12,
	CAMERA_YUY2,
	CAMERA_I420,
	CAMERA_UYVY,
};

enum CAMERA_PROPETIES
{
    CAMERA_BRIGHTNESS,
    CAMERA_CONTRAST,
    CAMERA_HUE,
    CAMERA_SATURATION,
    CAMERA_SHARPNESS,
    CAMERA_GAMMA,
    CAMERA_COLORENABLE,
    CAMERA_WHITEBALANCE,
    CAMERA_BACKLIGHTCOMPENSATION,
    CAMERA_GAIN,
    CAMERA_PAN,
    CAMERA_TILT,
    CAMERA_ROLL,
    CAMERA_ZOOM,
    CAMERA_EXPOSURE,
    CAMERA_IRIS,
    CAMERA_FOCUS,
    CAMERA_PROP_MAX
};

struct OutputCameraData
{
	int width;
	int height;
    CAMERA_COLOR_FORMAT format;
	uint8_t* data;
	unsigned long size;

};

class __declspec(dllexport) MFCameraCapture final
{
public:
    MFCameraCapture();
	~MFCameraCapture();

    int get_camera_count();
    void get_camera_id_list(std::vector<std::string>& id_list);
	bool start(const std::string& camera_id, int& width, int& height, CAMERA_COLOR_FORMAT& format);
	void stop(const std::string& camera_id);
    void get_resolution_list(const std::string& camera_id, std::vector<std::pair<int, int>>& resolution_list);
	void set_property(const std::string& camera_id, CAMERA_PROPETIES prop, int value);
	int get_property(const std::string& camera_id, CAMERA_PROPETIES prop);

    bool capture(const std::string& camera_id, OutputCameraData& output_data);

private:
    class Impl;
    Impl* impl_;
};

#endif