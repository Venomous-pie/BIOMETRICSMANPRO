#include "employee_db.h"
#include <ArduinoJson.h>

// ── Static employee list ──────────────────────────────────────────────────────
// Edit this JSON and re-flash to add or change employees.
// "id" must be consistent with the slot mapping formula in employee_db.h.
static const char EMPLOYEES_JSON[] = R"([
  {"id":1,"name":"Admin","dept":"Admin","job_title":"System Admin","branch":"Main","fp_enrolled":false},
  {"id":2,"name":"Christopher G. Francisco","dept":"Executive","job_title":"CIO","branch":"Main","fp_enrolled":false},
  {"id":3,"name":"Reden Lamosa","dept":"IT","job_title":"Senior Developer","branch":"Main","fp_enrolled":false},
  {"id":4,"name":"Jean Erica Velasco","dept":"IT","job_title":"Intern Lead","branch":"Main","fp_enrolled":false},
  {"id":5,"name":"Claire Jem Dedicatoria","dept":"IT","job_title":"Intern Technical Team Lead","branch":"Main","fp_enrolled":false},
  {"id":6,"name":"Maria Alaine Jeanne A. Terante","dept":"IT","job_title":"Assistant to Technical Team Lead","branch":"Main","fp_enrolled":false},
  {"id":7,"name":"Jhonnalyn Belano","dept":"IT","job_title":"QA","branch":"Main","fp_enrolled":false},
  {"id":8,"name":"Kenneth Simbolas","dept":"Hardware","job_title":"PCB Board Designer","branch":"Main","fp_enrolled":false},
  {"id":9,"name":"John Rustom Reginio","dept":"Hardware","job_title":"PCB Board Designer","branch":"Main","fp_enrolled":false},
  {"id":10,"name":"Sharlene Loria","dept":"Hardware","job_title":"Hardware","branch":"Main","fp_enrolled":false},
  {"id":11,"name":"Mark Jaestin Cabañelis","dept":"Design","job_title":"Figma / UI Design","branch":"Main","fp_enrolled":false}
])";

Employee empDB[MAX_EMP];
int      empCount = 0;

void employeeDbInit() {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, EMPLOYEES_JSON)) {
    Serial.println("[DB] ERROR: Employee JSON parse failed");
    return;
  }
  for (JsonObject e : doc.as<JsonArray>()) {
    if (empCount >= MAX_EMP) break;
    empDB[empCount].id          = e["id"].as<int>();
    empDB[empCount].name        = e["name"].as<String>();
    empDB[empCount].dept        = e["dept"].as<String>();
    empDB[empCount].job_title   = e.containsKey("job_title")   ? e["job_title"].as<String>()   : "";
    empDB[empCount].branch      = e.containsKey("branch")      ? e["branch"].as<String>()      : "";
    empDB[empCount].fp_enrolled = e.containsKey("fp_enrolled") ? e["fp_enrolled"].as<bool>()   : false;
    empCount++;
  }
  Serial.printf("[DB] %d employees loaded\n", empCount);
}

bool lookupEmployee(int slot, String &name, String &dept) {
  int emp_id = ((slot - 1) / 10) + 1;
  for (int i = 0; i < empCount; i++) {
    if (empDB[i].id == emp_id) {
      name = empDB[i].name;
      dept = empDB[i].dept;
      return true;
    }
  }
  return false;
}
