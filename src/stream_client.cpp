#include "stream_client.h"
#include "NativeHttpClient.h"
#include <WiFi.h>

static void parseBaseUrl(const char* baseUrl, String& host, uint16_t& port, String& basePath) {
  String u = String(baseUrl);
  while (u.endsWith("/")) u.remove(u.length() - 1);

  String scheme = "";
  if (u.startsWith("http://")) {
    scheme = "http";
    u.remove(0, 7);
  } else if (u.startsWith("https://")) {
    scheme = "https";
    u.remove(0, 8);
  }
  port = (scheme == "https") ? 443 : 80;

  int slash = u.indexOf('/');
  String hostPort;
  if (slash >= 0) {
    hostPort = u.substring(0, slash);
    basePath = u.substring(slash);
  } else {
    hostPort = u;
    basePath = "";
  }

  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    host = hostPort.substring(0, colon);
    port = (uint16_t)hostPort.substring(colon + 1).toInt();
  } else {
    host = hostPort;
  }
}

void StreamClient::begin(const char* baseUrl) {
  parseBaseUrl(baseUrl, host, port, basePath);
  rebuildCachedUrl();
}

void StreamClient::setBaseUrl(const char* baseUrl) {
  String newHost, newPath;
  uint16_t newPort;
  parseBaseUrl(baseUrl, newHost, newPort, newPath);
  host = newHost;
  port = newPort;
  basePath = newPath;
  rebuildCachedUrl();
}

void StreamClient::stop() {
  basePath = "";
  host = "";
  cachedStreamUrl = "";
  cachedCaptureUrl = "";
}

bool StreamClient::isStreaming() const {
  return host.length() > 0;
}

void StreamClient::rebuildCachedUrl() {
  if (host.length() == 0) {
    cachedStreamUrl = "";
    cachedCaptureUrl = "";
    return;
  }
  cachedStreamUrl  = "http://" + host + ":" + String(port) + basePath + "/stream";
  cachedCaptureUrl = "http://" + host + ":" + String(port) + basePath + "/capture";
}

bool StreamClient::sendFrame(const uint8_t* buf, size_t len, FrameSource src) {
  const String& url = (src == FrameSource::Stream) ? cachedStreamUrl : cachedCaptureUrl;
  if (url.length() == 0 || len == 0 || buf == nullptr) {
    return false;
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(2000);

  int code = http.POST((uint8_t*)buf, len);
  lastStatus = (uint16_t)(code > 0 ? code : 0);

  http.end();

  if (code >= 200 && code < 300) {
    framesSent++;
    return true;
  }

  failures++;
  Serial.printf("[STREAM] FAIL code=%d failures=%lu\n", code, failures);
  return false;
}

uint32_t StreamClient::getFramesSent() const { return framesSent; }
uint32_t StreamClient::getFailures()   const { return failures; }
uint16_t StreamClient::getLastStatus() const { return lastStatus; }
