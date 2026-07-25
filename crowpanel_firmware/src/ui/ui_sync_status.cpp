#include "ui_sync_status.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"

static lv_obj_t *scr             = NULL;
static lv_obj_t *btn_sync_ref    = NULL;  // kept so callbacks can re-enable it
static lv_obj_t *lbl_sync_ref    = NULL;  // button label
static lv_obj_t *sync_spinner    = NULL;  // arc spinner shown while syncing
static lv_obj_t *lbl_sync_status = NULL;  // text status line below the spinner
static lv_timer_t *sync_timeout  = NULL;  // auto-dismiss after 35s
static lv_obj_t *logs_cont       = NULL;  // container for recent activity logs
static lv_obj_t *cards_cont      = NULL;  // container for stat cards

// Persists across screen navigations so re-entry can restore spinner state
static bool g_sync_pending = false;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);

extern const lv_img_dsc_t icon_warning;

// ── Sync state helpers ──────────────────────────────────────────────────────
static void showSyncInProgress() {
    g_sync_pending = true;
    if (btn_sync_ref) {
        lv_obj_add_state(btn_sync_ref, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_sync_ref, UIManager::rgb(0xEEEEEE), 0);
    }
    if (lbl_sync_ref) {
        lv_label_set_text(lbl_sync_ref, LV_SYMBOL_REFRESH " Syncing...");
        UIManager::styleLabel(lbl_sync_ref, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    }
    if (sync_spinner)    lv_obj_clear_flag(sync_spinner, LV_OBJ_FLAG_HIDDEN);
    if (lbl_sync_status) {
        lv_obj_clear_flag(lbl_sync_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_sync_status, "Syncing time, employees & attendance...");
    }
}

static void clearSyncState(bool ok, const char* msg) {
    g_sync_pending = false;
    if (sync_timeout) { lv_timer_del(sync_timeout); sync_timeout = NULL; }
    if (sync_spinner)    lv_obj_add_flag(sync_spinner, LV_OBJ_FLAG_HIDDEN);
    if (btn_sync_ref) {
        lv_obj_clear_state(btn_sync_ref, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_sync_ref, UIManager::rgb(0xFFFFFF), 0);
    }
    if (lbl_sync_ref) {
        // Revert the button text to its actionable state so the user isn't clicking "Done" to sync again.
        // The success/fail message is still displayed in lbl_sync_status just below it.
        lv_label_set_text(lbl_sync_ref, LV_SYMBOL_REFRESH " Sync now");
        UIManager::styleLabel(lbl_sync_ref, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
        lv_obj_center(lbl_sync_ref);
    }
    if (lbl_sync_status) {
        lv_label_set_text(lbl_sync_status, msg);
        UIManager::styleLabel(lbl_sync_status,
            ok ? COLOR_GREEN_MAIN : COLOR_DANGER,
            &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    }
}

static void destroy_screen() {
    if (scr) {
        lv_obj_t *to_del = scr;
        scr             = NULL;
        btn_sync_ref    = NULL;
        lbl_sync_ref    = NULL;
        sync_spinner    = NULL;
        lbl_sync_status = NULL;
        logs_cont       = NULL;
        cards_cont      = NULL;
        // NOTE: do NOT reset g_sync_pending here — sync continues in background
        // NOTE: do NOT cancel sync_timeout here — it must still fire
        lv_obj_del_async(to_del);
    }
}

static void btn_back_cb(lv_event_t * e) {
    destroy_screen();
    UIManager::showSettings();
}

static void sync_timeout_cb(lv_timer_t *t) {
    // Sync took too long — auto-dismiss with error so user isn’t stuck
    if (Serial) Serial.println("[SYNC] Timed out waiting for WROOM response.");
    clearSyncState(false, "Timed out — tap Sync now to retry");
    sync_timeout = NULL; // already fired, cleared by lv_timer_del inside clearSyncState
}

static void btn_sync_now_cb(lv_event_t * e) {
    if (g_sync_pending) return; // debounce — ignore if already syncing
    if (Serial) Serial.println("UI Sync Status: Manual full sync requested");

    showSyncInProgress();

    // 35-second safety timeout — dismissed by uiSyncStatusOnSyncResult if WROOM replies first
    sync_timeout = lv_timer_create(sync_timeout_cb, 35000, NULL);
    lv_timer_set_repeat_count(sync_timeout, 1);

    // Upload any pending attendance logs first (runs in background FreeRTOS task).
    DataManager::uploadPendingLogs();

    // Then immediately request NTP + employee sync from WROOM.
    // These run on the WROOM's HTTP stack, separate from the CrowPanel's WiFi upload,
    // so there is no radio contention between the two.
    CommManager::sendCommand("{\"cmd\":\"SYNC_NTP\"}");
    String empCmd = "{\"cmd\":\"SYNC_EMP\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
    CommManager::sendCommand(empCmd);
}

void uiSyncStatusRefreshLogs();
void uiSyncStatusRefreshCards();

void buildSyncStatusScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    String subtitle = "Attendance logs & employees";
    UIManager::buildHeader(scr, "Sync Status", subtitle.c_str(), btn_back_cb, true);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 800, 408);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 20, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    int current_y = 10;
    
    // Top Alert Banner (if offline)
    bool isConnected = DataManager::isWifiConnected();
    if (!isConnected) {
        lv_obj_t *alert_banner = lv_obj_create(body);
        lv_obj_set_size(alert_banner, 450, 40);
        lv_obj_align(alert_banner, LV_ALIGN_TOP_LEFT, 0, current_y);
        lv_obj_set_style_bg_color(alert_banner, UIManager::rgb(0xFFE5E5), 0);
        lv_obj_set_style_border_width(alert_banner, 0, 0);
        lv_obj_set_style_radius(alert_banner, 4, 0);
        lv_obj_set_style_pad_all(alert_banner, 0, 0);
        lv_obj_clear_flag(alert_banner, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *alert_icon = lv_label_create(alert_banner);
        lv_label_set_text(alert_icon, LV_SYMBOL_WARNING);
        UIManager::styleLabel(alert_icon, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(alert_icon, LV_ALIGN_LEFT_MID, 10, 0);

        String alertMsg = "No connection";
        unsigned long dropTime = DataManager::getWifiDropTime();
        if (dropTime > 0) {
            // Simplified duration parsing; wait, we don't know the absolute time of drop if NTP failed, 
            // but we can just say "No connection. Attendance is still being recorded."
            alertMsg += ". Attendance is still being recorded";
        } else {
            alertMsg += ". Attendance is still being recorded";
        }

        lv_obj_t *alert_lbl = lv_label_create(alert_banner);
        lv_label_set_text(alert_lbl, alertMsg.c_str());
        UIManager::styleLabel(alert_lbl, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(alert_lbl, LV_ALIGN_LEFT_MID, 30, 0);
        
        current_y += 50;
    }

    // Cards Container
    cards_cont = lv_obj_create(body);
    lv_obj_set_size(cards_cont, 760, 110);
    lv_obj_align(cards_cont, LV_ALIGN_TOP_LEFT, 0, current_y);
    lv_obj_set_style_bg_opa(cards_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards_cont, 0, 0);
    lv_obj_set_style_pad_all(cards_cont, 0, 0);
    lv_obj_set_layout(cards_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cards_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cards_cont, LV_OBJ_FLAG_SCROLLABLE);

    uiSyncStatusRefreshCards();

    current_y += 120;
    
    // Sync Now button (static ref so callbacks can mutate it)
    btn_sync_ref = lv_btn_create(body);
    lv_obj_set_size(btn_sync_ref, 160, 36);
    lv_obj_align(btn_sync_ref, LV_ALIGN_TOP_RIGHT, 0, current_y);
    lv_obj_set_style_bg_color(btn_sync_ref, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(btn_sync_ref, UIManager::rgb(0xEEEEEE), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(btn_sync_ref, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_sync_ref, 1, 0);
    lv_obj_set_style_radius(btn_sync_ref, 6, 0);
    lv_obj_set_style_shadow_width(btn_sync_ref, 0, 0);
    lv_obj_add_event_cb(btn_sync_ref, btn_sync_now_cb, LV_EVENT_CLICKED, NULL);

    lbl_sync_ref = lv_label_create(btn_sync_ref);
    lv_label_set_text(lbl_sync_ref, LV_SYMBOL_REFRESH " Sync now");
    UIManager::styleLabel(lbl_sync_ref, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_sync_ref);

    // Arc spinner — hidden until a sync is in progress
    sync_spinner = lv_spinner_create(body, 1000, 60);
    lv_obj_set_size(sync_spinner, 30, 30);
    lv_obj_align(sync_spinner, LV_ALIGN_TOP_RIGHT, -170, current_y + 3);
    lv_obj_set_style_arc_color(sync_spinner, UIManager::rgb(COLOR_GREEN_MAIN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sync_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sync_spinner, UIManager::rgb(0xDDDDDD), LV_PART_MAIN);
    lv_obj_set_style_arc_width(sync_spinner, 3, LV_PART_MAIN);
    lv_obj_add_flag(sync_spinner, LV_OBJ_FLAG_HIDDEN);  // shown only while syncing

    // Status text under the spinner row (hidden by default)
    lbl_sync_status = lv_label_create(body);
    lv_label_set_text(lbl_sync_status, "");
    UIManager::styleLabel(lbl_sync_status, 0x888888, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(lbl_sync_status, LV_ALIGN_TOP_RIGHT, 0, current_y + 42);
    lv_obj_add_flag(lbl_sync_status, LV_OBJ_FLAG_HIDDEN);

    current_y += 50;

    // Restore visual state if a sync was in progress when user left and came back
    if (g_sync_pending) showSyncInProgress();

    // Recent activity Section
    lv_obj_t *lbl_activity = lv_label_create(body);
    lv_label_set_text(lbl_activity, "Recent activity");
    UIManager::styleLabel(lbl_activity, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_font(lbl_activity, &lv_font_montserrat_16, 0); // slightly bolder
    lv_obj_align(lbl_activity, LV_ALIGN_TOP_LEFT, 0, current_y);
    
    current_y += 30;

    logs_cont = lv_obj_create(body);
    lv_obj_set_size(logs_cont, 760, LV_SIZE_CONTENT);
    lv_obj_align(logs_cont, LV_ALIGN_TOP_LEFT, 0, current_y);
    lv_obj_set_style_bg_opa(logs_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logs_cont, 0, 0);
    lv_obj_set_style_pad_all(logs_cont, 0, 0);
    lv_obj_clear_flag(logs_cont, LV_OBJ_FLAG_SCROLLABLE);

    uiSyncStatusRefreshLogs();
}

void uiSyncStatusRefreshCards() {
    if (!cards_cont) return;
    lv_obj_clean(cards_cont);

    bool isConnected = DataManager::isWifiConnected();
    int pending = DataManager::getUnsyncedAttendanceCount();
    unsigned long lastSyncMs = DataManager::getLastSyncTimestamp();
    int empCount = DataManager::getEmployeeCount();

    auto create_stat_card = [](lv_obj_t *parent, const char *title, const char *value, bool danger) -> lv_obj_t* {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 175, 100);
        lv_obj_set_style_bg_color(card, UIManager::rgb(danger ? 0xFFF0F0 : 0xFFFFFF), 0);
        lv_obj_set_style_border_color(card, UIManager::rgb(danger ? 0xFFD0D0 : COLOR_STROKE), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, title);
        UIManager::styleLabel(lbl_title, 0x888888, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *lbl_val = lv_label_create(card);
        lv_label_set_text(lbl_val, value);
        
        UIManager::styleLabel(lbl_val, danger ? 0xA02020 : 0x777777, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
        if (!danger && strcmp(title, "Pending records") == 0) {
            lv_obj_set_style_text_color(lbl_val, UIManager::rgb(0xA05020), 0); // Orange-ish for pending
        }
        lv_obj_align(lbl_val, LV_ALIGN_TOP_LEFT, 0, 30);
        
        return card;
    };

    // Card 1: Pending records
    create_stat_card(cards_cont, "Pending records", String(pending).c_str(), false);
    
    // Card 2: Offline for
    String offlineStr = "Online";
    bool isOfflineDanger = false;
    if (!isConnected) {
        isOfflineDanger = true;
        unsigned long dropMs = DataManager::getWifiDropTime();
        if (dropMs == 0) dropMs = millis(); // If booted offline
        unsigned long offlineMs = millis() - dropMs;
        int mins = (offlineMs / 60000) % 60;
        int hrs = (offlineMs / 3600000);
        if (hrs > 0) {
            offlineStr = String(hrs) + "hrs " + String(mins) + "min";
        } else {
            offlineStr = String(mins) + " mins";
        }
    }
    create_stat_card(cards_cont, isConnected ? "Network" : "Offline for", offlineStr.c_str(), isOfflineDanger);
    
    // Card 3: Last synced
    String lastSyncStr = "Never";
    if (lastSyncMs > 0) {
        unsigned long elapsed = millis() - lastSyncMs;
        int mins = (elapsed / 60000) % 60;
        int hrs = (elapsed / 3600000);
        if (hrs > 0) {
            lastSyncStr = String(hrs) + "h " + String(mins) + "m ago";
        } else {
            lastSyncStr = String(mins) + "m ago";
        }
    }
    create_stat_card(cards_cont, "Last synced", lastSyncStr.c_str(), false);
    
    // Card 4: Employees
    create_stat_card(cards_cont, "Employees", String(empCount).c_str(), false);
}

void uiSyncStatusRefreshLogs() {
    if (!logs_cont) return;
    lv_obj_clean(logs_cont);

    int logCount = DataManager::getSyncLogCount();
    const DataManager::SyncLogEntry* logs = DataManager::getSyncLogs();
    
    if (logCount == 0) {
        lv_obj_t *lbl_no_logs = lv_label_create(logs_cont);
        lv_label_set_text(lbl_no_logs, "No recent activity");
        UIManager::styleLabel(lbl_no_logs, 0xAAAAAA, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl_no_logs, LV_ALIGN_TOP_LEFT, 20, 0);
    } else {
        int inner_y = 0;
        // Iterate backwards (newest first)
        for (int i = logCount - 1; i >= 0; i--) {
            lv_obj_t *row = lv_obj_create(logs_cont);
            lv_obj_set_size(row, 760, 40);
            lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, inner_y);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(row, UIManager::rgb(COLOR_STROKE), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            if (i != 0) {
                lv_obj_set_style_border_width(row, 1, 0); // bottom border for all except last (chronological)
            }
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            String msgStr = logs[i].message;
            const char* symbol = LV_SYMBOL_REFRESH;
            int color = 0x3BB143; // Default green
            
            if (msgStr.indexOf("fail") >= 0 || msgStr.indexOf("lost") >= 0) {
                symbol = LV_SYMBOL_WARNING;
                color = COLOR_DANGER;
            } else if (msgStr.indexOf("Connection") >= 0) {
                symbol = LV_SYMBOL_WIFI;
            } else if (msgStr.indexOf("Uploaded") >= 0) {
                symbol = LV_SYMBOL_UPLOAD;
                color = 0x2196F3;
            } else if (msgStr.indexOf("Time") >= 0) {
                symbol = LV_SYMBOL_SETTINGS;
            } else if (msgStr.indexOf("Synced") >= 0) {
                symbol = LV_SYMBOL_DOWNLOAD;
                color = 0x2196F3;
            } else {
                symbol = LV_SYMBOL_OK;
            }

            lv_obj_t *icon = lv_label_create(row);
            lv_label_set_text(icon, symbol);
            UIManager::styleLabel(icon, color, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

            lv_obj_t *msg = lv_label_create(row);
            lv_label_set_text(msg, logs[i].message.c_str());
            UIManager::styleLabel(msg, 0x333333, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
            lv_obj_align(msg, LV_ALIGN_LEFT_MID, 40, 0);

            lv_obj_t *time_lbl = lv_label_create(row);
            unsigned long elapsed = millis() - logs[i].timestamp;
            int mins = (elapsed / 60000) % 60;
            int hrs = (elapsed / 3600000);
            String timeStr = "";
            if (hrs > 0) timeStr = String(hrs) + "h " + String(mins) + "m ago";
            else timeStr = String(mins) + "m ago";

            lv_label_set_text(time_lbl, timeStr.c_str());
            UIManager::styleLabel(time_lbl, 0xAAAAAA, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
            lv_obj_align(time_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

            inner_y += 40;
        }
    }
}

void uiShowSyncStatus() {
    if (!scr) buildSyncStatusScreen();
    uiSyncStatusRefreshCards(); // Make sure stat cards are live
    uiSyncStatusRefreshLogs();  // Update relative timestamps
    lv_scr_load(scr);
}

// Called by CommManager when EMP_SYNC_DONE or EMP_SYNC_FAIL arrives from WROOM
void uiSyncStatusOnSyncResult(bool ok) {
    // Always clear global state, even if screen is not currently open.
    // The individual widget checks inside clearSyncState handle NULL pointers safely.
    const char* msg = ok ? "All data synced successfully" : "Sync failed — check connection";
    clearSyncState(ok, msg);
    uiSyncStatusRefreshCards(); // Update stats now that sync is complete
}
