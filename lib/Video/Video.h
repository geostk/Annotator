#pragma once

extern "C" {
#include <libavutil/motion_vector.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#ifdef _MSC_VER
#define IMQS_VIDEO_API __declspec(dllimport)
#else
#define IMQS_VIDEO_API
#endif

#include "Decode.h"