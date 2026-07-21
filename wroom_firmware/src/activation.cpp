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

// ── Inline Base64 encoder ─────────────────────────────────────────────────────
// Encodes binary data to Base64 string (RFC 4648).
// Returns the encoded string.
static String base64Encode(const uint8_t* data, size_t len) {
  static const char tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = (uint32_t)data[i] << 16;
    if (i + 1 < len) b |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) b |= (uint32_t)data[i + 2];
    out += tbl[(b >> 18) & 0x3F];
    out += tbl[(b >> 12) & 0x3F];
    out += (i + 1 < len) ? tbl[(b >>  6) & 0x3F] : '=';
    out += (i + 2 < len) ? tbl[(b      ) & 0x3F] : '=';
  }
  return out;
}

// ── Upload enrollment ─────────────────────────────────────────────────────────
// POST /api/devices/employees/enroll
// Body:
// {
//   "employee_name": "<name>",
//   "finger_index":  <0-9>,
//   "slot":          <1-127>,       // AS608 physical slot number
//   "device_id":     "<DEVICE_ID>",
//   "template_data": "<base64>",    // 512-byte (or 768-byte) AS608 template
//   "template_size": <int>
// }
void uploadEnrollment(const String& deviceToken,
                      const String& empName,
                      int fingerIndex,
                      int slot,
                      const uint8_t* templateBytes,
                      int templateLen) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ENROLL_UPLOAD] WiFi not connected — skipping upload.");
    return;
  }
  if (deviceToken.length() == 0) {
    Serial.println("[ENROLL_UPLOAD] No device token — skipping upload.");
    return;
  }

  String templateB64 = (templateLen > 0)
      ? base64Encode(templateBytes, templateLen)
      : String("");

  // Build JSON body. DynamicJsonDocument because template_data can be ~700 chars.
  DynamicJsonDocument body(2048);
  body["employee_name"]  = empName;
  body["finger_index"]   = fingerIndex;
  body["slot"]           = slot;
  body["device_id"]      = DEVICE_ID;
  body["template_data"]  = templateB64;
  body["template_size"]  = templateLen;

  String bodyStr;
  serializeJson(body, bodyStr);

  String url = String(API_BASE_URL) + "/api/devices/employees/enroll";
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + deviceToken);

  int httpCode = http.POST(bodyStr);
  Serial.printf("[ENROLL_UPLOAD] POST %s → HTTP %d\n", url.c_str(), httpCode);
  if (httpCode > 0) {
    Serial.println("[ENROLL_UPLOAD] Response: " + http.getString());
  }
  http.end();
}



