#pragma once

#define IDD_PROMPT        101
#define IDD_ACTION        102
#define IDD_PRESETS       103
#define IDD_HOST          104
#define IDR_SIGNING_CERT  110
#define IDR_OUI           111  // MAC vendor table (res/oui.bin, tools/make-oui.py)
#define IDI_APP           1
#define IDD_STARTTASK     105
#define IDD_SCANRESULT    106
#define IDC_AUTOSTART     1040
#define IDC_SHORTCUT      1041

#define IDC_PROMPT_LABEL  1001
#define IDC_PROMPT_EDIT   1002

#define IDC_TYPE          1010
#define IDC_HOST          1011
#define IDC_PORT          1012
#define IDC_USER          1013
#define IDC_PASS          1014
#define IDC_MODE          1015
#define IDC_LBL_PORT      1016
#define IDC_LBL_USER      1017
#define IDC_LBL_MODE      1018
#define IDC_LBL_PASS      1019
#define IDC_LBL_VNC       1030
#define IDC_VNCPATH       1031
#define IDC_BROWSE        1032
#define IDC_LBL_HOST      1033
#define IDC_HOST_NAME     1034

#define IDC_PRESET_LIST   1020
#define IDC_DELETE        1021
#define IDC_EXPORT        1022
#define IDC_IMPORT        1023

#ifndef IDC_STATIC
#define IDC_STATIC        (-1)
#endif

#define IDC_SCAN_SUMMARY  1042
#define IDC_SCAN_LIST     1043
#define IDC_COPY          1044
#define IDC_ADD_SELECTED  1045
#define IDC_ADD_ALL       1046
#define IDC_HOST_PING     1047
