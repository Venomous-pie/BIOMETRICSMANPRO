#include "activation.h"
#include "comms.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

void validateActivationWithServer(const String &registrationCode) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ACTIVATION] Cannot validate — WiFi not connected.");
    send("{\"type\":\"ACTIVATION_RESULT\",\"success\":false,\"err\":\"WiFi not connected\"}");
    return;
  }

  Serial.printf("[ACTIVATION] Validating with server. device_id=%s  code=%s\n",
                DEVICE_ID, registrationCode.c_str());

  // Query string format as expected by the backend API endpoint.
  String url = String(API_BASE_URL)
             + "/api/devices/registerDevice"
             + "?device_id=" + DEVICE_ID
             + "&registration_code=" + registrationCode;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(""); // credentials are in the query string; body is empty
  Serial.printf("[ACTIVATION] HTTP %d\n", httpCode);

  StaticJsonDocument<256> result;
  result["type"] = "ACTIVATION_RESULT";

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("[ACTIVATION] Response: " + response);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, response);

    bool   success  = false;
    String devToken = "";
    String errMsg   = "Code rejected by server";

    if (err == DeserializationError::Ok) {
      String statusStr  = doc["status"] | "";
      int    statusCode = doc["status"] | 0;
      // Accept any of the success indicators the server may return.
      if (statusCode == 200 ||
          statusStr.equalsIgnoreCase("success") ||
          statusStr.equalsIgnoreCase("active") ||
          statusStr.equalsIgnoreCase("true") ||
          doc["success"].as<bool>()) {
        success  = true;
        devToken = doc["device_token"] | "";
      } else {
        errMsg = doc["message"] | errMsg;
      }
    } else {
      Serial.println("[ACTIVATION] Could not parse server response.");
    }

    result["success"] = success;
    if (success) result["device_token"] = devToken;
    else         result["err"]          = errMsg;

  } else {
    Serial.printf("[ACTIVATION] Request failed: %s\n", http.errorToString(httpCode).c_str());
    result["success"] = false;
    result["err"]     = "Server unreachable";
  }

  http.end();
  sendDoc(result);
}

void testApiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TEST_API] Cannot test — WiFi not connected.");
    send("{\"type\":\"TEST_RESULT\",\"success\":false,\"err\":\"WiFi not connected\"}");
    return;
  }
  
  String url = String(API_BASE_URL);
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  
  StaticJsonDocument<128> result;
  result["type"] = "TEST_RESULT";
  
  if (httpCode > 0) {
    result["success"] = true;
    result["msg"] = String("HTTP ") + String(httpCode);
  } else {
    result["success"] = false;
    result["err"] = http.errorToString(httpCode);
  }
  http.end();
  sendDoc(result);
}
