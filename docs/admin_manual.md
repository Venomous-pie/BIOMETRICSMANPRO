# Biometrics Manpro System - Administrator Manual

Welcome to the Administrator Manual for the Biometrics Time-In/Time-Out System. This guide provides an overview of the device's user interface, detailing how to navigate through the various screens and manage the device effectively.

---

## 1. Initial Setup Screens

When you power on the device for the first time (or after a factory reset), you will be guided through an initial setup sequence.

### Wi-Fi Setup Screen
*   **Purpose**: Connects the device to your local network.
*   **Usage**: Select your Wi-Fi network from the list of available networks and enter the password. A successful connection is required for cloud syncing and remote management.

### Device Activation Screen
*   **Purpose**: Links the hardware to your company's account.
*   **Usage**: Enter the provided secure activation code/token. Once validated, the device will unlock and load the standard operating interface.

---

## 2. Standard Operation

### Idle / Standby Screen
*   **Purpose**: The default state of the device, designed for everyday employee interaction.
*   **What it shows**: The current time, date, and a prompt indicating that the device is ready to read a fingerprint.
*   **Action**: Employees place their finger on the scanner. The screen will transition to the **Result Screen** upon a scan.
*   **Admin Access**: Administrators can access the Main Menu by tapping a specific area (often a gear icon or a long-press on the screen, depending on your exact configuration) and entering the admin PIN.

### Scan Result Screen
*   **Purpose**: Provides immediate visual feedback to the employee.
*   **Success**: Displays a green/teal confirmation indicating a successful "Time In" or "Time Out", along with the employee's name.
*   **Failure**: Displays a red warning (e.g., "Fingerprint Not Recognized" or "Please Try Again") if the scan fails or the employee is not enrolled.
*   **Timeout**: This screen will automatically return to the Idle Screen after a few seconds.

---

## 3. Administrator Main Menu

The Main Menu is the central hub for all administrative tasks. From here, you can navigate to the three primary management modules: **Enrollment**, **Logs**, and **Settings**.

### Enrollment Screen
*   **Purpose**: Add new employees to the biometric database.
*   **Usage**: 
    1. Enter the Employee ID or Name.
    2. Follow the on-screen prompts instructing the user to place their finger on the scanner multiple times (usually 3 times) to capture a reliable template.
    3. Save the profile. The employee can now use the Idle Screen to clock in/out.

### Attendance Logs
*   **Purpose**: View a localized history of recent time-in and time-out events.
*   **Usage**: Scroll through the list of recent scans to verify attendance or troubleshoot missed punches. (Note: Complete logs are typically synced to the main server dashboard).

---

## 4. Device Settings

The Settings Hub allows you to configure the hardware and system preferences.

### Display Settings
*   **Brightness**: Slide to adjust the screen backlight intensity.
*   **Screen Timeout**: Set how long the screen stays awake before dimming or turning off to save power (e.g., 30 seconds, 1 minute, or Never).

### Clock & Date Settings
*   **Purpose**: Ensure the time logs are accurate.
*   **Usage**: You can manually set the time and date, or ensure the device is successfully pulling the time via NTP (Network Time Protocol) over Wi-Fi.

### Server Configuration
*   **Purpose**: Manage backend connectivity.
*   **Usage**: Configure API endpoints, sync intervals, or update the target server where attendance data is sent. *Only modify these if instructed by IT support.*

### Danger Zone / Reset
*   **Purpose**: Destructive actions that cannot be undone.
*   **Usage**: 
    *   **Clear All Data**: Erases all enrolled fingerprints and local logs.
    *   **Factory Reset**: Wipes all Wi-Fi settings, activation tokens, and employee data, returning the device to its out-of-the-box state. 
    *   *A warning prompt will ask for confirmation before executing these actions.*

---

*For further technical support or issues not covered in this manual, please contact your system provider.*
