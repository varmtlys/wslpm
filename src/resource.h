#pragma once

// ── Control IDs ──────────────────────────────────────────

// Sidebar
#define IDC_DISK_LISTBOX         1001

// Mount form — disk/partition
#define IDC_COMBO_DISK           1002
#define IDC_COMBO_PARTITION      1003

// Volume type radios
#define IDC_RADIO_AUTO           1010
#define IDC_RADIO_LUKS           1011
#define IDC_RADIO_LVM            1012
#define IDC_RADIO_EXT4           1013
#define IDC_RADIO_XFS            1014
#define IDC_RADIO_BTRFS          1015
#define IDC_RADIO_NTFS           1016
#define IDC_RADIO_VFAT           1017

// Password Dialog
#define IDD_DIALOG_PASSWORD      2000
#define IDC_EDIT_PWD_INPUT       2001
#define IDC_STATIC_TEXT          2002

#define IDC_EDIT_KEYFILE         1024
#define IDC_BTN_BROWSE_KEY       1025

// Mount options
#define IDC_EDIT_MOUNTPOINT      1030
#define IDC_BTN_CREATE_DIR       1037
#define IDC_COMBO_DISTRO         1033
#define IDC_CHECK_SHORTCUT       1034
#define IDC_CHECK_READONLY       1035
#define IDC_BTN_MOUNT            1036

// Mounted volumes
#define IDC_MOUNTED_LV           1040
#define IDC_BTN_UNMOUNT_SEL      1041
#define IDC_BTN_EJECT_SEL        1042
#define IDC_BTN_OPEN_SEL         1043

// Log
#define IDC_EDIT_LOG             1070
#define IDC_BTN_COPY_LOG         1071

// Toolbar
#define IDC_BTN_REFRESH          1050
#define IDC_BTN_UNMOUNT_ALL      1051
#define IDC_BTN_COMPACT          1052
#define IDC_PROGRESS             1053

// Compact dialog
#define IDD_DIALOG_COMPACT       2100
#define IDC_LIST_IMAGES          2101
#define IDC_CHECK_ZEROFREE       2102
#define IDC_STATIC_COMPACT_INFO  2103

// Status
#define IDC_STATUSBAR            1060

// ── Custom window messages ───────────────────────────────
#define WM_APP_DISKS_LOADED      (WM_APP + 1)
#define WM_APP_DISTROS_LOADED    (WM_APP + 2)
#define WM_APP_MOUNT_DONE        (WM_APP + 3)
#define WM_APP_UNMOUNT_DONE      (WM_APP + 4)
#define WM_APP_ENV_CHECKED       (WM_APP + 5)
#define WM_APP_COMMAND_LOG       (WM_APP + 6)
#define WM_APP_COMPACT_DONE      (WM_APP + 7)
#define WM_APP_COMPACT_PROGRESS  (WM_APP + 8)
