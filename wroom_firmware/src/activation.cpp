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

void syncEmployeesFromServer(const String &token) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] fetchEmployees: WiFi not connected");
    send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"WiFi not connected\"}");
    return;
  }
  if (token.length() == 0) {
    Serial.println("[API] fetchEmployees: No activation token");
    send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"No activation token\"}");
    return;
  }

  Serial.println("[API] Fetching employees from server...");
  String url = String(API_BASE_URL) + "/api/devices/employees";

  HTTPClient http;
  http.begin(url);
  http.setTimeout(15000); // 15 seconds timeout
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Accept", "application/json");

  int httpCode = http.GET();
  Serial.printf("[API] HTTP GET Response Code: %d\n", httpCode);

  if (httpCode == 200 || httpCode == 201) {
    send("{\"type\":\"EMP_SYNC_START\"}");
    delay(50); // give CP time to clear its temp DB

    String payload = http.getString();
    http.end(); // CRITICAL: Release Wi-Fi TCP resources before blasting ESP-NOW!

    DynamicJsonDocument dDoc(16384);
    DeserializationError err = deserializeJson(dDoc, payload);

    if (err == DeserializationError::Ok) {
      JsonArray arr = dDoc["employees"].as<JsonArray>();
      if (arr.isNull()) {
        Serial.println("[API] ERROR: Response JSON does not contain 'employees' array!");
        send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"Invalid JSON response\"}");
      } else {
        int count = 0;
        for (JsonObject e : arr) {
          // Packet 1: Send the massive ID
          StaticJsonDocument<512> p1;
          p1["type"] = "E1";
          p1["id"] = e["id"].as<String>();
          sendDoc(p1);
          delay(60);

          // Packet 2: Send the metadata
          StaticJsonDocument<256> p2;
          p2["type"] = "E2";
          
          String first = e.containsKey("first_name") ? e["first_name"].as<String>() : "";
          String last  = e.containsKey("last_name") ? e["last_name"].as<String>() : "";
          p2["n"] = first + " " + last;
          
          p2["d"] = e.containsKey("department_name") ? e["department_name"].as<String>() : "";
          p2["j"] = e.containsKey("role_name") ? e["role_name"].as<String>() : "";
          p2["b"] = e.containsKey("branch_name") ? e["branch_name"].as<String>() : "";
          
          sendDoc(p2);
          count++;
          delay(40); // Base delay between packets
          
          if (count % 10 == 0) {
            send("{\"type\":\"EMP_BATCH_DONE\"}");
            Serial.println("[API] Sent batch of 10, waiting for ACK...");
            
            unsigned long waitStart = millis();
            bool ackReceived = false;
            while (millis() - waitStart < 3000) {
               if (!cpQueueEmpty()) {
                 String msg = cpQueuePop();
                 if (msg.indexOf("\"EMP_BATCH_ACK\"") != -1) {
                   ackReceived = true;
                   break;
                 }
                 // If not an ACK, put it back or handle it in main loop later.
                 // For now, we drop other messages during sync to prevent queue stalls.
               }
               delay(10); 
            }
            if (!ackReceived) {
               Serial.println("[API] Timeout waiting for batch ACK! Aborting sync.");
               send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"CrowPanel unreachable\"}");
               return; // abort the entire sync function
            } else {
               Serial.println("[API] Batch ACK received!");
            }
          }
        }
        Serial.printf("[API] Successfully streamed %d employees to CrowPanel.\n", count);
        send("{\"type\":\"EMP_SYNC_DONE\"}");
      }
    } else {
      Serial.printf("[API] JSON Parse failed: %s\n", err.c_str());
      send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"JSON parse failed\"}");
    }
  } else {
    Serial.println("[API] Server error: " + http.getString());
    send("{\"type\":\"EMP_SYNC_FAIL\",\"msg\":\"Server error\"}");
  }
  http.end();
}
