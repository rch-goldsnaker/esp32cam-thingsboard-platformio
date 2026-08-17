#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <array>
#include <Server_Side_RPC.h>
#include <Shared_Attribute_Update.h>
#include <Attribute_Request.h>
#include <ThingsBoard.h>

#include "camera_module.h"
#include "stream_client.h"

void processSetFlash(const JsonVariantConst& data, JsonDocument& response);
void processStartStream(const JsonVariantConst& data, JsonDocument& response);
void processStopStream(const JsonVariantConst& data, JsonDocument& response);
void processCapture(const JsonVariantConst& data, JsonDocument& response);

void processSharedAttributes(const JsonObjectConst& data);
void processClientAttributes(const JsonObjectConst& data);

void requestTimedOut();

bool isStreamUrlFromShared();
void resetStreamUrlReceived();
bool isSubscriptionsActive();
void setSubscriptionsActive();

const std::array<RPC_Callback, 4U>& getRpcCallbacks();
const std::array<const char*, 10U>& getSharedAttributesList();

void bindCallbacksGlobals(ThingsBoardSized<32U>* tb, CameraModule* cam, StreamClient* str);

void markStreamFailed();
bool isStreamInBackoff();
