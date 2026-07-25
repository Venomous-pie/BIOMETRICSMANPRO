#ifndef UI_SYNC_STATUS_H
#define UI_SYNC_STATUS_H

void uiShowSyncStatus();
void uiSyncStatusOnSyncResult(bool ok);  // Call when EMP_SYNC_DONE/FAIL arrives

#endif // UI_SYNC_STATUS_H
