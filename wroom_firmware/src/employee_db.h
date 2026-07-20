#pragma once
#include <Arduino.h>

// Employee record — mirrors the backend data model.
//
// Slot-to-employee mapping formula:
//   slot = ((emp_id - 1) * 10) + finger_index + 1
// This allows up to 10 fingerprint templates per employee (finger indexes 0–9).
struct Employee {
  String id;
  String name;
  String dept;
  String job_title;
  String branch;
  bool   fp_enrolled;
};

const int MAX_EMP = 10;

extern Employee empDB[MAX_EMP];
extern int      empCount;

// Parses EMPLOYEES_JSON and populates empDB[].
// Call once from setup().
void employeeDbInit();

// Looks up the employee name and department for a given AS608 slot number.
// Returns true if a matching employee record exists, false otherwise.
bool lookupEmployee(int slot, String &name, String &dept);
