#include <WiFi.h>

#include <Arduino_MQTT_Client.h>
#include <Server_Side_RPC.h>
#include <Shared_Attribute_Update.h>
#include <Attribute_Request.h>
#include <ThingsBoard.h>
#include <Telemetry.h>
#include <array>

#include "secrets.h"
#include "config_keys.h"
#include "camera_pins.h"
#include "camera_module.h"
#include "stream_client.h"
#include "tb_callbacks.h"
#include <esp_psram.h>
#include <esp_heap_caps.h>

const char WIFI_SSID[]         = WIFI_SSID_TXT;
const char WIFI_PASSWORD[]     = WIFI_PASSWORD_TXT;
const char TOKEN[]             = TB_TOKEN_TXT;
constexpr char TB_SERVER[]      = "thingsboard.cloud";
constexpr uint16_t TB_PORT      = 1883U;

constexpr uint32_t SERIAL_DEBUG_BAUD    = 115200U;
constexpr uint16_t TELEMETRY_INTERVAL   = 2000U;
constexpr uint16_t TELEMETRY_SLOW_INTERVAL = 10000U;
constexpr size_t MAX_ATTRIBUTES         = 10U;
constexpr uint64_t ATTR_REQUEST_TIMEOUT = 5000ULL * 1000ULL;

volatile bool streamEnabled = false;
volatile uint16_t streamFps = 5;
volatile bool flashState    = false;

uint32_t lastFrameMs    = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastSlowTelemetryMs = 0;

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
CameraModule camera;
StreamClient streamer;

Server_Side_RPC<4U, 5U> rpc;
Attribute_Request<2U, MAX_ATTRIBUTES> attrRequest;
Shared_Attribute_Update<3U, MAX_ATTRIBUTES> sharedUpdate;

const std::array<IAPI_Implementation*, 3U> apis = {
  &rpc,
  &attrRequest,
  &sharedUpdate
};

ThingsBoardSized<32U> tb(mqttClient, 1024U, Default_Max_Stack_Size, apis);

const Shared_Attribute_Callback<MAX_ATTRIBUTES> sharedAttrCallback(
  &processSharedAttributes,
  getSharedAttributesList().cbegin(),
  getSharedAttributesList().cend()
);

const Attribute_Request_Callback<MAX_ATTRIBUTES> sharedAttrRequestCallback(
  &processSharedAttributes,
  ATTR_REQUEST_TIMEOUT,
  &requestTimedOut,
  getSharedAttributesList()
);

void sendStaticMetrics() {
  std::array<Telemetry, 1U> heap = {
    Telemetry("heapSize", (int)ESP.getHeapSize())
  };
  tb.sendAttributes<1U>(heap.begin(), heap.end());

  if (psramFound()) {
    std::array<Telemetry, 1U> psram = {
      Telemetry("psramTotal", (int)esp_psram_get_size())
    };
    tb.sendAttributes<1U>(psram.begin(), psram.end());
  }
}

void initWiFi() {
  Serial.print("[BOOT] Connecting to AP: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    Serial.println();
    Serial.println("[BOOT] Connected to AP");
    Serial.println("[BOOT] WiFi sleep disabled");
    Serial.print("[BOOT] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[BOOT] WiFi timeout, continuing");
  }
}

void connectThingsBoard() {
  Serial.print("[TB] Connecting to: ");
  Serial.print(TB_SERVER);
  Serial.print(":");
  Serial.println(TB_PORT);
  if (!tb.connect(TB_SERVER, TOKEN, TB_PORT)) {
    Serial.println("[TB] Failed to connect");
    return;
  }
  tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
  sendStaticMetrics();
  if (!isSubscriptionsActive()) {
    if (!rpc.RPC_Subscribe(getRpcCallbacks().cbegin(), getRpcCallbacks().cend())) {
      Serial.println("[TB] RPC subscribe failed");
      return;
    }
    if (!sharedUpdate.Shared_Attributes_Subscribe(sharedAttrCallback)) {
      Serial.println("[TB] shared attributes subscribe failed");
      return;
    }
    resetStreamUrlReceived();
    if (!attrRequest.Shared_Attributes_Request(sharedAttrRequestCallback)) {
      Serial.println("[TB] shared attributes request failed");
      return;
    }
    setSubscriptionsActive();
    Serial.println("[TB] Subscribe done");
  }
}

void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  delay(200);
  Serial.println("\n=========================");
  Serial.println("[BOOT] ESP32-CAM starting");
  Serial.print("[BOOT] Baud: ");
  Serial.println(SERIAL_DEBUG_BAUD);

  bindCallbacksGlobals(&tb, &camera, &streamer);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  initWiFi();

  if (!camera.begin()) {
    Serial.println("[CAM] init FAILED - continuing without camera");
  }

  streamer.begin("");

  Serial.println("[STREAM] Waiting for streamUrl from ThingsBoard shared attributes...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastWifiReconnectMs = 0;
    static uint8_t wifiAttempt = 0;
    uint32_t now = millis();
    uint32_t interval = min((uint32_t)30000U, (uint32_t)(5000U * (wifiAttempt + 1)));
    if (now - lastWifiReconnectMs < interval) return;
    lastWifiReconnectMs = now;
    wifiAttempt++;
    if (wifiAttempt > 6) wifiAttempt = 6;
    initWiFi();
    if (WiFi.status() == WL_CONNECTED) wifiAttempt = 0;
    return;
  }

  if (!tb.connected()) {
    static uint32_t lastReconnectMs = 0;
    uint32_t now = millis();
    if (now - lastReconnectMs < 5000) return;
    lastReconnectMs = now;
    connectThingsBoard();
  }

  tb.loop();

  uint32_t now = millis();

  if (streamEnabled && camera.isReady() && isStreamUrlFromShared()) {
    if (!isStreamInBackoff()) {
      uint16_t fps = max((uint16_t)1, (uint16_t)streamFps);
      uint32_t periodMs = 1000UL / fps;
      if (now - lastFrameMs > periodMs) {
        lastFrameMs = now;
        const uint8_t* buf = nullptr;
        size_t len = 0;
        if (camera.captureJpeg(&buf, &len)) {
          if (!streamer.sendFrame(buf, len, FrameSource::Stream)) {
            markStreamFailed();
            Serial.printf("[STREAM] fail backoff 5s (total=%lu)\n", streamer.getFailures());
          }
          camera.releaseFrameBuffer();
        }
      }
    }
  }

  if (now - lastTelemetryMs > TELEMETRY_INTERVAL) {
    lastTelemetryMs = now;
    std::array<Telemetry, 2U> fast = {
      Telemetry("rssi", WiFi.RSSI()),
      Telemetry("streamFailures", (int)streamer.getFailures())
    };
    tb.sendAttributes<2U>(fast.begin(), fast.end());
  }

  if (now - lastSlowTelemetryMs > TELEMETRY_SLOW_INTERVAL) {
    lastSlowTelemetryMs = now;
    size_t idx = 0;
    std::array<Telemetry, 8U> slow = {};
    slow[idx++] = Telemetry("uptime", (int)(now / 1000));
    slow[idx++] = Telemetry("streamFramesSent", (int)streamer.getFramesSent());
    slow[idx++] = Telemetry("freeHeap", (int)ESP.getFreeHeap());
    slow[idx++] = Telemetry("heapMinFree", (int)heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));
    if (psramFound()) {
      size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      size_t psramTotal = esp_psram_get_size();
      int psramUsedPct = psramTotal > 0 ? (int)((psramTotal - psramFree) * 100 / psramTotal) : 0;
      slow[idx++] = Telemetry("freePsram", (int)psramFree);
      slow[idx++] = Telemetry("psramUsedPct", psramUsedPct);
    }
    if (camera.isReady()) {
      slow[idx++] = Telemetry("lastJpegSize", (int)camera.getLastJpegSize());
      slow[idx++] = Telemetry("cameraReady", true);
    } else {
      slow[idx++] = Telemetry("cameraReady", false);
    }
    tb.sendAttributes<8U>(slow.begin(), slow.begin() + idx);
  }
}
