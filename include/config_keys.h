#pragma once

// Shared attributes (ThingsBoard -> device)
constexpr const char* ATTR_STREAM_ENABLED = "streamEnabled";
constexpr const char* ATTR_STREAM_URL     = "streamUrl";
constexpr const char* ATTR_STREAM_FPS     = "streamFps";
constexpr const char* ATTR_FRAME_SIZE     = "frameSize";
constexpr const char* ATTR_IMAGE_QUALITY  = "imageQuality";
constexpr const char* ATTR_VFLIP          = "vflip";
constexpr const char* ATTR_HMIRROR        = "hmirror";
constexpr const char* ATTR_BRIGHTNESS     = "brightness";
constexpr const char* ATTR_CONTRAST       = "contrast";
constexpr const char* ATTR_SATURATION     = "saturation";

// RPC methods (ThingsBoard -> device)
constexpr const char* RPC_SET_FLASH    = "setFlash";
constexpr const char* RPC_START_STREAM = "startStream";
constexpr const char* RPC_STOP_STREAM  = "stopStream";
constexpr const char* RPC_CAPTURE      = "capture";
