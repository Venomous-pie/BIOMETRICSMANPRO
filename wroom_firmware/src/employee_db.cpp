#include "employee_db.h"
#include <ArduinoJson.h>

// ── Static employee list ──────────────────────────────────────────────────────
// Edit this JSON and re-flash to add or change employees.
// "id" must be consistent with the slot mapping formula in employee_db.h.
Employee empDB[MAX_EMP];
int      empCount = 0;

void employeeDbInit() {
  Serial.println("[DB] WROOM employee DB initialized (empty). Master DB is on CrowPanel.");
}

bool lookupEmployee(int slot, String &name, String &dept) {
  // WROOM no longer stores employees. Returns false to force fallback to "Slot X"
  return false;
}
