#pragma once
#include <esp_camera.h>
#include <Arduino.h>

class CameraModule {
public:
  bool begin();
  void end();
  bool isReady() const;

  bool setFrameSize(uint8_t f);
  bool setQuality(uint8_t q);
  bool setVFlip(bool on);
  bool setHMirror(bool on);
  bool setBrightness(int v);
  bool setContrast(int v);
  bool setSaturation(int v);

  bool captureJpeg(const uint8_t** outBuf, size_t* outLen);
  void releaseFrameBuffer();

  uint8_t getCurrentFrameSize() const;
  size_t getLastJpegSize() const;

private:
  bool ready = false;
  uint8_t currentFrameSize = 8;
  size_t lastJpegSize = 0;
  camera_fb_t* currentFb = nullptr;
};
