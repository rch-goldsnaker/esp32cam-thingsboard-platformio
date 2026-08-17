#pragma once
#include <Arduino.h>

enum class FrameSource {
  Stream,
  Capture
};

class StreamClient {
public:
  void begin(const char* baseUrl);
  void setBaseUrl(const char* baseUrl);
  void stop();
  bool isStreaming() const;

  bool sendFrame(const uint8_t* buf, size_t len, FrameSource src);

  uint32_t getFramesSent() const;
  uint32_t getFailures() const;
  uint16_t getLastStatus() const;

private:
  void rebuildCachedUrl();

  String host;
  uint16_t port = 80;
  String basePath;
  String cachedStreamUrl;
  String cachedCaptureUrl;
  uint32_t framesSent = 0;
  uint32_t failures   = 0;
  uint16_t lastStatus = 0;
};
