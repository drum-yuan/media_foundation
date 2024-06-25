#ifndef MF_CAPTURE_MONITOR_H
#define MF_CAPTURE_MONITOR_H

#include <string>

enum MONITOR_COLOR_FORMAT
{
	MONITOR_NONE = 0,
	MONITOR_BGRA,
	MONITOR_D3D11,
};

struct OutputMonitorData
{
	int width;
	int height;
	int stride;
	MONITOR_COLOR_FORMAT format;
	uint8_t* data;
	unsigned long size;
};

class __declspec(dllexport) MFMonitorCapture final
{
public:
	MFMonitorCapture();
	~MFMonitorCapture();

	int get_monitor_count();
	void* get_monitor_handle(int index);
	std::string get_monitor_name(int index);
	void get_monitor_resolution(void* hmon, int& width, int& height);
	bool start(void* hmon, bool show_cursor, MONITOR_COLOR_FORMAT format);
	void stop();

	bool capture(OutputMonitorData& output_data);

private:
	class Impl;
	Impl* impl_;
};

#endif