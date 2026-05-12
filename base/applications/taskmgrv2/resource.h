#pragma once

/* -----------------------------------------------------------------------
 * resource.h — taskmgrv2 resource ID master header
 * Range layout (no overlap with classic taskmgr):
 *   100..199   top-level app resources
 *   1000..1099 dialog templates (IDD_*)
 *   1100..1199 control IDs (IDC_*)
 *   1200..1299 menu / command IDs (IDM_*, IDC_CTX_*)
 *   1300..1499 string table (IDS_*)
 *   1500..1599 bitmap / icon IDs (IDB_*, IDI_*)
 * ----------------------------------------------------------------------- */

/* Top-level */
#define IDI_TASKMGRV2           100
#define IDR_MAINFRAME           101
#define IDR_TASKMGRV2_MENU      102
#define IDA_PERF                103   /* accelerator table for perf page */

/* Dialogs */
#define IDD_SETTINGS            1000

/* Controls */
#define IDC_SETTINGS_THEME_LIGHT     1100
#define IDC_SETTINGS_THEME_DARK      1101
#define IDC_SETTINGS_THEME_SYSTEM    1102
#define IDC_SETTINGS_REFRESH_LOW     1103
#define IDC_SETTINGS_REFRESH_MED     1104
#define IDC_SETTINGS_REFRESH_HIGH    1105
#define IDC_SETTINGS_DEFAULTPAGE     1106
#define IDC_SETTINGS_USERS_FULLNAME  1107
#define IDC_SETTINGS_REALTIME_WARN   1108

/* Context menu / command IDs */
#define IDC_CTX_MODE_OVERALL    1210
#define IDC_CTX_MODE_LOGICAL    1211
#define IDC_CTX_MODE_NUMA       1212
#define IDC_CTX_KERNEL          1220
#define IDC_CTX_SUMMARY         1221
#define IDC_CTX_REFRESH         1230
#define IDC_CTX_SPEED_MENU      1231
#define IDC_CTX_SPEED_HIGH      1232
#define IDC_CTX_SPEED_NORMAL    1233
#define IDC_CTX_SPEED_LOW       1234
#define IDC_CTX_SPEED_PAUSED    1235
#define IDC_CTX_COPY            1240

/* String table */
#define IDS_APP_TITLE           1300
#define IDS_NAV_PROCESSES       1301
#define IDS_NAV_PERFORMANCE     1302
#define IDS_NAV_APPHISTORY      1303
#define IDS_NAV_STARTUP         1304
#define IDS_NAV_USERS           1305
#define IDS_NAV_DETAILS         1306
#define IDS_NAV_SERVICES        1307
#define IDS_NAV_SETTINGS        1308
#define IDS_PERFCAT_CPU         1310
#define IDS_PERFCAT_MEMORY      1311
#define IDS_PERFCAT_DISK        1312
#define IDS_PERFCAT_NET         1313
#define IDS_PERFCAT_GPU         1314
#define IDS_STAT_UTILIZATION    1320
#define IDS_STAT_SPEED          1321
#define IDS_STAT_PROCESSES      1322
#define IDS_STAT_THREADS        1323
#define IDS_STAT_HANDLES        1324
#define IDS_STAT_UPTIME         1325
#define IDS_STAT_MAXSPEED       1326
#define IDS_STAT_SOCKETS        1327
#define IDS_STAT_CORES          1328
#define IDS_STAT_LOGICAL_PROCS  1329
#define IDS_STAT_VIRT           1330
#define IDS_STAT_L1CACHE        1331
#define IDS_STAT_L2CACHE        1332
#define IDS_STAT_L3CACHE        1333
#define IDS_STUB_PAGE_FMT       1340
#define IDS_SETTINGS_THEME      1341
#define IDS_SETTINGS_REFRESH    1342
#define IDS_SETTINGS_DEFPAGE    1343
#define IDS_SETTINGS_USERS      1344
#define IDS_SETTINGS_REALTIME   1345
#define IDS_THEME_LIGHT         1346
#define IDS_THEME_DARK          1347
#define IDS_THEME_SYSTEM        1348
#define IDS_REFRESH_LOW         1349
#define IDS_REFRESH_MED         1350
#define IDS_REFRESH_HIGH        1351

/* Bitmap / icon IDs */
#define IDB_NAV_STRIP           1500
