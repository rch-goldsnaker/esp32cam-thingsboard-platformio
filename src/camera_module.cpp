#include "camera_module.h"
#include "camera_pins.h"
#include <esp_system.h>
#include <esp_camera.h>
#include <esp_psram.h>
#include <Arduino.h>

#define CAMERA_DEFAULT_FRAMESIZE 8  // SVGA (800x600)
#define CAMERA_DEFAULT_QUALITY   12 // 0=best ... 63=worst

bool CameraModule::begin() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = (framesize_t)CAMERA_DEFAULT_FRAMESIZE;
  config.jpeg_quality = CAMERA_DEFAULT_QUALITY;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (!psramFound()) {
    config.fb_location    = CAMERA_FB_IN_DRAM;
    config.fb_count       = 1;
    config.jpeg_quality   = 12;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] init failed: 0x%x\n", err);
    ready = false;
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_vflip(s, 0);
    s->set_hmirror(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_aec2(s, 1);
    s->set_gainceiling(s, (gainceiling_t)6);
  }

  ready = true;
  Serial.println("[CAM] ready");
  return true;
}

void CameraModule::end() {
  esp_camera_deinit();
  ready = false;
}

bool CameraModule::isReady() const {
  return ready;
}

bool CameraModule::setFrameSize(uint8_t f) {
  if (f > 13) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  if (s->set_framesize(s, (framesize_t)f) == 0) {
    currentFrameSize = f;
    return true;
  }
  return false;
}

bool CameraModule::setQuality(uint8_t q) {
  if (q > 63) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_quality(s, q) == 0;
}

bool CameraModule::setVFlip(bool on) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_vflip(s, (int)on) == 0;
}

bool CameraModule::setHMirror(bool on) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_hmirror(s, (int)on) == 0;
}

bool CameraModule::setBrightness(int v) {
  if (v < -2 || v > 2) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_brightness(s, v) == 0;
}

bool CameraModule::setContrast(int v) {
  if (v < -2 || v > 2) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_contrast(s, v) == 0;
}

bool CameraModule::setSaturation(int v) {
  if (v < -2 || v > 2) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_saturation(s, v) == 0;
}

bool CameraModule::captureJpeg(const uint8_t** outBuf, size_t* outLen) {
  if (!ready) return false;
  releaseFrameBuffer();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  if (fb->format != PIXFORMAT_JPEG || fb->len == 0) {
    esp_camera_fb_return(fb);
    return false;
  }
  lastJpegSize = fb->len;
  currentFb = fb;
  *outBuf = fb->buf;
  *outLen = fb->len;
  return true;
}

void CameraModule::releaseFrameBuffer() {
  if (currentFb) {
    esp_camera_fb_return(currentFb);
    currentFb = nullptr;
  }
}

uint8_t CameraModule::getCurrentFrameSize() const {
  return currentFrameSize;
}

size_t CameraModule::getLastJpegSize() const {
  return lastJpegSize;
}
