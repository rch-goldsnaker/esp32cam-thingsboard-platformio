#include "tb_callbacks.h"
#include "config_keys.h"
#include "camera_pins.h"
#include "camera_module.h"
#include "stream_client.h"
#include <ThingsBoard.h>
#include <WiFi.h>

static CameraModule* g_cam = nullptr;
static StreamClient* g_stream = nullptr;
static bool streamUrlReceived = false;
static bool subscriptionsActive = false;
static uint32_t sLastStreamFailMs = 0;

extern volatile bool streamEnabled;
extern volatile bool flashState;

void bindCallbacksGlobals(ThingsBoardSized<32U>* tb, CameraModule* cam, StreamClient* str) {
  g_cam = cam; g_stream = str;
}

void markStreamFailed() {
  sLastStreamFailMs = millis();
}

bool isStreamInBackoff() {
  return sLastStreamFailMs > 0 && (millis() - sLastStreamFailMs) < 5000;
}

bool isSubscriptionsActive() {
  return subscriptionsActive;
}

void setSubscriptionsActive() {
  subscriptionsActive = true;
}

void clearSubscriptionsActive() {
  subscriptionsActive = false;
}

static bool parseFlashValue(const JsonVariantConst& data) {
  if (data.is<bool>()) return data.as<bool>();
  if (data.is<int>())  return data.as<int>() != 0;
  if (data.is<const char*>()) {
    const char* s = data.as<const char*>();
    return (strcmp(s, "false") != 0 && strcmp(s, "0") != 0 && strcmp(s, "off") != 0);
  }
  if (data.is<JsonObjectConst>()) {
    JsonObjectConst obj = data.as<JsonObjectConst>();
    if (obj.containsKey("state")) {
      JsonVariantConst v = obj["state"];
      if (v.is<bool>()) return v.as<bool>();
      if (v.is<int>())  return v.as<int>() != 0;
      if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return (strcmp(s, "false") != 0 && strcmp(s, "0") != 0 && strcmp(s, "off") != 0);
      }
    }
  }
  return false;
}

void processSetFlash(const JsonVariantConst& data, JsonDocument& response) {
  bool on = parseFlashValue(data);
  flashState = on;
  digitalWrite(FLASH_LED_PIN, on ? HIGH : LOW);
  Serial.printf("[RPC] setFlash -> %s (pin %d)\n", on ? "ON" : "OFF", FLASH_LED_PIN);
  response["flash"] = on;
}

void processStartStream(const JsonVariantConst& data, JsonDocument& response) {
  (void)data;
  Serial.println("[RPC] startStream invoked");
  streamEnabled = true;
  response["streamEnabled"] = true;
}

void processStopStream(const JsonVariantConst& data, JsonDocument& response) {
  (void)data;
  Serial.println("[RPC] stopStream invoked");
  streamEnabled = false;
  response["streamEnabled"] = false;
}

void processCapture(const JsonVariantConst& data, JsonDocument& response) {
  (void)data;
  Serial.println("[RPC] capture invoked");
  if (!g_cam || !g_cam->isReady()) {
    response["ok"] = false;
    response["error"] = g_cam ? "camera_not_ready" : "camera_module_null";
    return;
  }
  if (!isStreamUrlFromShared()) {
    response["ok"] = false;
    response["error"] = "stream_url_not_configured";
    return;
  }
  if (isStreamInBackoff()) {
    response["ok"] = false;
    response["error"] = "server_unreachable_backoff";
    Serial.println("[RPC] capture rejected: server in backoff");
    return;
  }
  const uint8_t* buf = nullptr;
  size_t len = 0;
  if (!g_cam->captureJpeg(&buf, &len)) {
    response["ok"] = false;
    response["error"] = "capture_failed";
    return;
  }
  bool ok = g_stream->sendFrame(buf, len, FrameSource::Capture);
  if (!ok) markStreamFailed();
  g_cam->releaseFrameBuffer();
  response["ok"]     = ok;
  response["bytes"]  = (int)len;
  response["status"] = g_stream->getLastStatus();
}

void processSharedAttributes(const JsonObjectConst& data) {
  Serial.print("[TB] shared attrs: ");
  serializeJson(data, Serial);
  Serial.println();

  for (auto it = data.begin(); it != data.end(); ++it) {
    const char* key = it->key().c_str();
    JsonVariantConst v = it->value();

    if (strcmp(key, ATTR_STREAM_ENABLED) == 0) {
      streamEnabled = v.as<bool>();
      Serial.printf("[TB] streamEnabled=%d\n", streamEnabled);
    } else if (strcmp(key, ATTR_STREAM_URL) == 0) {
      const char* url = v.as<const char*>();
      if (url && url[0] != '\0' && g_stream) {
        g_stream->begin(url);
        streamUrlReceived = true;
        Serial.printf("[TB] streamUrl=%s\n", url);
      }
    } else if (strcmp(key, ATTR_STREAM_FPS) == 0) {
      uint16_t f = v.as<uint16_t>();
      if (f >= 1 && f <= 15) {
        extern volatile uint16_t streamFps;
        streamFps = f;
      }
    } else if (strcmp(key, ATTR_FRAME_SIZE) == 0) {
      if (g_cam) g_cam->setFrameSize(v.as<uint8_t>());
    } else if (strcmp(key, ATTR_IMAGE_QUALITY) == 0) {
      if (g_cam) g_cam->setQuality(v.as<uint8_t>());
    } else if (strcmp(key, ATTR_VFLIP) == 0) {
      if (g_cam) g_cam->setVFlip(v.as<bool>());
    } else if (strcmp(key, ATTR_HMIRROR) == 0) {
      if (g_cam) g_cam->setHMirror(v.as<bool>());
    } else if (strcmp(key, ATTR_BRIGHTNESS) == 0) {
      if (g_cam) g_cam->setBrightness(v.as<int>());
    } else if (strcmp(key, ATTR_CONTRAST) == 0) {
      if (g_cam) g_cam->setContrast(v.as<int>());
    } else if (strcmp(key, ATTR_SATURATION) == 0) {
      if (g_cam) g_cam->setSaturation(v.as<int>());
    }
  }
}

void processClientAttributes(const JsonObjectConst& data) {
  (void)data;
}

void requestTimedOut() {
  Serial.println("[TB] attribute request timed out");
}

const std::array<RPC_Callback, 4U>& getRpcCallbacks() {
  static const std::array<RPC_Callback, 4U> callbacks = {
    RPC_Callback{ RPC_SET_FLASH,    processSetFlash },
    RPC_Callback{ RPC_START_STREAM, processStartStream },
    RPC_Callback{ RPC_STOP_STREAM,  processStopStream },
    RPC_Callback{ RPC_CAPTURE,      processCapture },
  };
  return callbacks;
}

const std::array<const char*, 10U>& getSharedAttributesList() {
  static const std::array<const char*, 10U> list = {
    ATTR_STREAM_ENABLED, ATTR_STREAM_URL, ATTR_STREAM_FPS,
    ATTR_FRAME_SIZE, ATTR_IMAGE_QUALITY,
    ATTR_VFLIP, ATTR_HMIRROR,
    ATTR_BRIGHTNESS, ATTR_CONTRAST, ATTR_SATURATION
  };
  return list;
}

bool isStreamUrlFromShared() {
  return streamUrlReceived;
}

void resetStreamUrlReceived() {
  streamUrlReceived = false;
}
