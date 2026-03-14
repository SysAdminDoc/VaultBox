// VaultBox Desktop - Resource IDs
#pragma once

// Icon
#define IDI_VAULTBOX          101

// Tray
#define WM_TRAYICON          (WM_USER + 1)
#define WM_VAULTBOX_QUIT     (WM_USER + 2)
#define WM_VAULTBOX_LOG      (WM_USER + 3)
#define WM_VAULTBOX_REFRESH  (WM_USER + 4)

// Menu IDs
#define IDM_FILE_IMPORT_BW_JSON   2001
#define IDM_FILE_IMPORT_BW_CSV    2002
#define IDM_FILE_IMPORT_CHROME    2003
#define IDM_FILE_IMPORT_KEEPASS   2004
#define IDM_FILE_EXPORT_JSON      2010
#define IDM_FILE_EXPORT_CSV       2011
#define IDM_FILE_LOCK             2020
#define IDM_FILE_QUIT             2030

#define IDM_EDIT_ADD_ENTRY        2101
#define IDM_EDIT_EDIT_ENTRY       2102
#define IDM_EDIT_DELETE_ENTRY     2103
#define IDM_EDIT_COPY_USER        2104
#define IDM_EDIT_COPY_PASS        2105
#define IDM_EDIT_LAUNCH_URI       2106
#define IDM_EDIT_ADD_FOLDER       2107
#define IDM_EDIT_RENAME_FOLDER    2108
#define IDM_EDIT_DELETE_FOLDER    2109

#define IDM_TOOLS_PASSGEN         2201
#define IDM_TOOLS_OPEN_DATA       2202
#define IDM_TOOLS_TOGGLE_LOG      2203

#define IDM_HELP_ABOUT            2301

// Tray context menu
#define IDM_TRAY_SHOW             2401
#define IDM_TRAY_QUIT             2402

// Control IDs
#define IDC_TREEVIEW              3001
#define IDC_LISTVIEW              3002
#define IDC_DETAIL_PANEL          3003
#define IDC_LOG_PANEL             3004
#define IDC_SPLITTER_V            3005
#define IDC_SPLITTER_H            3006
#define IDC_STATUSBAR             3007

// Unlock dialog
#define IDC_UNLOCK_EMAIL          4001
#define IDC_UNLOCK_PASSWORD       4002
#define IDC_UNLOCK_OK             4003

// Entry dialog
#define IDC_ENTRY_NAME            4101
#define IDC_ENTRY_USERNAME        4102
#define IDC_ENTRY_PASSWORD        4103
#define IDC_ENTRY_URI             4104
#define IDC_ENTRY_NOTES           4105
#define IDC_ENTRY_FOLDER          4106
#define IDC_ENTRY_TYPE            4107
#define IDC_ENTRY_SHOW_PASS       4108
#define IDC_ENTRY_GEN_PASS        4109
#define IDC_ENTRY_OK              4110
#define IDC_ENTRY_CANCEL          4111

// Folder dialog
#define IDC_FOLDER_NAME           4201
#define IDC_FOLDER_OK             4202
#define IDC_FOLDER_CANCEL         4203

// Password generator dialog
#define IDC_PG_LENGTH_SLIDER      4301
#define IDC_PG_LENGTH_LABEL       4302
#define IDC_PG_UPPER              4303
#define IDC_PG_LOWER              4304
#define IDC_PG_DIGITS             4305
#define IDC_PG_SYMBOLS            4306
#define IDC_PG_AMBIGUOUS          4307
#define IDC_PG_PREVIEW            4308
#define IDC_PG_GENERATE           4309
#define IDC_PG_COPY               4310
#define IDC_PG_OK                 4311
#define IDC_PG_USE                4312

// Detail panel labels
#define IDC_DET_NAME_LBL          4401
#define IDC_DET_NAME_VAL          4402
#define IDC_DET_USER_LBL          4403
#define IDC_DET_USER_VAL          4404
#define IDC_DET_USER_COPY         4405
#define IDC_DET_PASS_LBL          4406
#define IDC_DET_PASS_VAL          4407
#define IDC_DET_PASS_COPY         4408
#define IDC_DET_PASS_SHOW         4409
#define IDC_DET_URI_LBL           4410
#define IDC_DET_URI_VAL           4411
#define IDC_DET_URI_LAUNCH        4412
#define IDC_DET_NOTES_LBL         4413
#define IDC_DET_NOTES_VAL         4414

// About dialog
#define IDC_ABOUT_OK              4501
