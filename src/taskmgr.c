/**
 * ============================================================================
 * Windows Native Task Manager en C (Win32 API)
 * ----------------------------------------------------------------------------
 * Compilable sans dépendance tierce avec MinGW-w64 :
 *   gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32
 *
 * Fonctionnalités implémentées :
 * - Réduction en barre des tâches (System Tray Icon) lors de la fermeture (WM_CLOSE)
 * - Icône Tray dynamique 3 couleurs générée en mémoire (CPU vert/orange/rouge, RAM violet, GPU cyan)
 * - Double-clic sur l'icône Tray pour réafficher et restaurer l'application
 * - Menu clic droit sur l'icône Tray : "Ouvrir" et "Quitter définitivement"
 * - Correction des en-têtes : les noms de colonnes restent TOUJOURS affichés
 * - Indicateur visuel explicite de colonne triée (symbole ▲ / ▼ + style HDF_SORT)
 * - Tri interactif multi-colonnes par clic d'en-tête (CPU, RAM, Disque, Réseau, GPU, PID, Nom, etc.)
 * - Masquer un processus individuel par son nom (clic droit -> "Masquer <nom> par son nom")
 * - Bouton "Exclusions (N)" avec menu de gestion pour réafficher ou effacer les exclusions
 * - Persistance automatique de tous les réglages et processus exclus dans settings.ini
 * - Toggle pour masquer / afficher les processus système de base de Windows (via signature & chemin)
 * - Moteur différentiel SANS scintillement (in-place update anti-flicker double-buffered)
 * - Métriques complètes : CPU %, GPU % réel (PDH WDDM), Mémoire (Mo), CLI Args, PID, Disque & Réseau
 * - Changement de priorité (Temps réel, Haute, Supérieure, Normale, Inférieure, Basse)
 * - Terminaison brutale immédiate sans confirmation via TerminateProcess(hProcess, 1)
 * - Statistiques système globales en temps réel (CPU global, RAM, Processus, I/O)
 * - Bouton de bascule rafraîchissement automatique à 1 seconde (ON / OFF)
 * - Raccourci clavier Suppr (VK_DELETE) pour tuer immédiatement sans confirmation
 * - Menu contextuel clic droit avec changement de priorité, copie des CLI args et exclusions
 * ============================================================================
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601 // Windows 7 ou supérieur

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "resource.h"
#include "version.h"

#ifdef _MSC_VER
// Activation du style visuel Windows moderne sous MSVC
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// Définitions pour les flèches d'en-tête de colonnes
#ifndef HDF_SORTUP
#define HDF_SORTUP 0x0400
#endif
#ifndef HDF_SORTDOWN
#define HDF_SORTDOWN 0x0200
#endif

// Identifiants des contrôles UI
#define IDC_LISTVIEW         1001
#define IDC_STATUSBAR        1002
#define IDC_BTN_REFRESH      1003
#define IDC_BTN_AUTOREFRESH  1004
#define IDC_EDIT_SEARCH      1006
#define IDC_STATIC_SEARCH    1007
#define IDC_BTN_PRIORITY     1008
#define IDC_BTN_HIDE_WINDOWS 1009
#define IDC_BTN_EXCLUSIONS   1010
#define IDC_GRAPH_BANNER     1011
#define IDC_BTN_SETTINGS     1012
#define IDC_BTN_ABOUT        1013

// Contrôles des fenêtres modales Paramètres et À propos
#define IDC_SET_COMBO_LANG   6001
#define IDC_SET_CHK_AUTO     6002
#define IDC_SET_CHK_HIDEWIN  6003
#define IDC_SET_ACTIVE_LANG  6004

// Identifiants du Timer
#define IDT_REFRESH_TIMER    2001
#define IDT_TRAY_RETRY_TIMER 2002
#define REFRESH_INTERVAL_MS  1000

// Message personnalisé et identifiants pour la zone de notification (Tray Icon)
#define WM_TRAYICON               (WM_USER + 100)
#define IDM_TRAY_OPEN             4001
#define IDM_TRAY_EXIT             4002

// Identifiants du Menu Contextuel et Commandes
#define IDM_KILL_IMMEDIATE        3001
#define IDM_PRIORITY_REALTIME     3002
#define IDM_PRIORITY_HIGH         3003
#define IDM_PRIORITY_ABOVENORM    3004
#define IDM_PRIORITY_NORMAL       3005
#define IDM_PRIORITY_BELOWNORM    3006
#define IDM_PRIORITY_IDLE         3007
#define IDM_COPY_CMDLINE          3008
#define IDM_OPEN_LOCATION         3009
#define IDM_TOGGLE_AUTO           3010
#define IDM_TOGGLE_HIDE_WIN       3011
#define IDM_HIDE_BY_NAME          3020
#define IDM_MANAGE_EXCLUSIONS     3021
#define IDM_CLEAR_ALL_EXCLUSIONS  3022
#define IDM_RESTORE_EXCLUSION_BASE 3100 // 3100 à 3355 pour restaurer des processus individuels

// Colonnes de la ListView
enum {
    COL_NAME = 0,
    COL_PID,
    COL_CPU,
    COL_RAM,
    COL_DISK,
    COL_NET,
    COL_GPU,
    COL_PRIORITY,
    COL_CMDLINE,
    COL_COUNT
};

// Langues prises en charge
typedef enum {
    APP_LANG_AUTO = 0,
    APP_LANG_EN   = 1,
    APP_LANG_FR   = 2,
    APP_LANG_ES   = 3,
    APP_LANG_DE   = 4,
    APP_LANG_IT   = 5,
    APP_LANG_PT   = 6
} AppLanguage;

// Prototypes des fonctions d'accès aux ressources et multilingue
int GetEffectiveLanguage(void);
const WCHAR* GetResString(UINT uID);
const WCHAR* GetColumnTitle(int col);
void ApplyLanguageToUI(HWND hWnd);
void ShowSettingsDialog(HWND hParent);
void ShowAboutDialog(HWND hParent);

// Structure de stockage de métriques d'un processus
typedef struct {
    DWORD pid;
    WCHAR szName[MAX_PATH];
    WCHAR szCmdLine[1024];
    WCHAR szFullPath[MAX_PATH];
    double cpuPercent;
    SIZE_T workingSetMB;
    double diskMBPerSec;
    double netKBPerSec;
    double gpuPercent;
    DWORD priorityClass;
    BOOL isWindowsBase;
} ProcessEntry;

// Historique global pour le calcul des deltas
typedef struct {
    DWORD pid;
    ULONGLONG kernelTime;
    ULONGLONG userTime;
    ULONGLONG readBytes;
    ULONGLONG writeBytes;
    ULONGLONG otherBytes;
    ULONGLONG timestamp;
} ProcessHistory;

#define MAX_HISTORY_PROCESSES 4096
static ProcessHistory g_History[MAX_HISTORY_PROCESSES];
static int g_HistoryCount = 0;

static ULONGLONG g_LastSysIdleTime = 0;
static ULONGLONG g_LastSysKernelTime = 0;
static ULONGLONG g_LastSysUserTime = 0;
static ULONGLONG g_LastRefreshTime = 0;

// Stockage dynamique en mémoire pour le tri et la synchronisation anti-scintillement
static ProcessEntry g_Processes[MAX_HISTORY_PROCESSES];
static int g_ProcessCount = 0;
static ProcessEntry* g_FilteredList[MAX_HISTORY_PROCESSES];
static int g_FilteredCount = 0;

// Gestion des processus exclus par leur nom
#define MAX_EXCLUDED_PROCESSES 256
static WCHAR g_ExcludedProcesses[MAX_EXCLUDED_PROCESSES][MAX_PATH];
static int g_ExcludedCount = 0;

// État des réglages
static int g_SortColumn = COL_CPU; // Tri par défaut sur le CPU
static BOOL g_bSortAscending = FALSE; // Tri décroissant (processus les plus actifs en premier)
static BOOL g_bHideWindowsProcesses = FALSE; // Toggle pour masquer les processus de base Windows
static BOOL g_bAutoRefresh = TRUE; // Auto-refresh à 1s
static int g_nLanguageSetting = APP_LANG_AUTO; // Langue (Auto = système, repli Anglais)

// Identifiants et état des 3 Tray Icons (CPU, RAM, GPU) avec historique
#define TRAY_ID_CPU 1
#define TRAY_ID_RAM 2
#define TRAY_ID_GPU 3

#define TRAY_HISTORY_POINTS 14 // 14 points d'historique temporels (colonnes de pixels x=1 à 14 dans 16x16)

static double g_TrayCpuHistory[TRAY_HISTORY_POINTS] = { 0 };
static double g_TrayRamHistory[TRAY_HISTORY_POINTS] = { 0 };
static double g_TrayGpuHistory[TRAY_HISTORY_POINTS] = { 0 };
static int g_TrayHistoryCount = 0;

// Historique étendu sur 1 heure (3600 secondes) pour les graphiques de performances au-dessus des processus
#define PERF_HISTORY_POINTS 3600

// Échantillon de processus consommateur le plus actif à une seconde précise
typedef struct {
    WCHAR szName[32];
    DWORD pid;
    float usage; // % pour CPU/GPU, Mo pour RAM
} TopProcSample;

static double g_PerfCpuHistory[PERF_HISTORY_POINTS] = { 0 };
static double g_PerfRamHistory[PERF_HISTORY_POINTS] = { 0 };
static double g_PerfGpuHistory[PERF_HISTORY_POINTS] = { 0 };

static TopProcSample g_TopCpuHistory[PERF_HISTORY_POINTS];
static TopProcSample g_TopRamHistory[PERF_HISTORY_POINTS];
static TopProcSample g_TopGpuHistory[PERF_HISTORY_POINTS];
static int g_PerfHistoryCount = 0;

static double g_LastCpuPct = 0.0;
static double g_LastMemPct = 0.0;
static double g_LastGpuPct = 0.0;
static double g_LastUsedRamGb = 0.0;
static double g_LastTotalRamGb = 0.0;

// Télémétrie GPU réelle via PDH ("\GPU Engine(*)\Utilization Percentage", WDDM 2.0+)
typedef PDH_STATUS (WINAPI *pfnPdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS (WINAPI *pfnPdhAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI *pfnPdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *pfnPdhGetFormattedCounterArrayW)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
typedef PDH_STATUS (WINAPI *pfnPdhCloseQuery)(PDH_HQUERY);

typedef struct {
    DWORD pid;
    double gpuPercent;
} GpuProcSample;

typedef struct {
    WCHAR szEngineKey[64];
    double totalPercent;
} GpuEngineTotal;

typedef struct {
    DWORD pid;
    int engineIdx;
    double percent;
} GpuProcEnginePair;

#define MAX_GPU_ENGINES 256
#define MAX_GPU_PROC_SAMPLES MAX_HISTORY_PROCESSES
#define MAX_GPU_PROC_ENGINE_PAIRS 2048
#define GPU_SAMPLE_MIN_INTERVAL_MS 750

static HMODULE g_hPdhDll = NULL;
static pfnPdhOpenQueryW g_pfnPdhOpenQueryW = NULL;
static pfnPdhAddEnglishCounterW g_pfnPdhAddEnglishCounterW = NULL;
static pfnPdhCollectQueryData g_pfnPdhCollectQueryData = NULL;
static pfnPdhGetFormattedCounterArrayW g_pfnPdhGetFormattedCounterArrayW = NULL;
static pfnPdhCloseQuery g_pfnPdhCloseQuery = NULL;

static PDH_HQUERY g_hGpuQuery = NULL;
static PDH_HCOUNTER g_hGpuCounter = NULL;
static BOOL g_bGpuPdhReady = FALSE;
static BOOL g_bGpuBaselineCollected = FALSE;
static ULONGLONG g_LastGpuCollectTick = 0;

static BYTE *g_pGpuPdhBuf = NULL;
static DWORD g_GpuPdhBufCap = 0;

static GpuProcSample g_CachedGpuProcs[MAX_GPU_PROC_SAMPLES];
static int g_CachedGpuProcCount = 0;
static double g_CachedTotalGpuPct = 0.0;

static BOOL g_bInTray = FALSE;
static BOOL g_bTrayCpuCreated = FALSE;
static BOOL g_bTrayRamCreated = FALSE;
static BOOL g_bTrayGpuCreated = FALSE;

static HICON g_hIconCpu = NULL;
static HICON g_hIconRam = NULL;
static HICON g_hIconGpu = NULL;

// Icônes de l'application Task Manager
#define IDI_APP_ICON 1
static HICON g_hAppIcon = NULL;
static HICON g_hAppIconSm = NULL;

// Variables globales UI
static HWND g_hMainWnd = NULL;
static HWND g_hListView = NULL;
static HWND g_hStatusBar = NULL;
static HWND g_hGraphBanner = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnAutoRefresh = NULL;
static HWND g_hBtnHideWindows = NULL;
static HWND g_hBtnExclusions = NULL;
static HWND g_hStaticSearch = NULL;
static HWND g_hEditSearch = NULL;
static HWND g_hBtnSettings = NULL;
static HWND g_hBtnAbout = NULL;
static WCHAR g_szSearchFilter[128] = { 0 };
static HFONT g_hFont = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;

// Instance unique (Single Instance) & Restauration de la barre des tâches (Explorer crash)
static HANDLE g_hSingleInstanceMutex = NULL;
static UINT g_uMsgRestoreInstance = 0;
static UINT g_uMsgTaskbarCreated = 0;

// Pointeur dynamique vers NtQueryInformationProcess
typedef NTSTATUS (NTAPI *pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);
static pfnNtQueryInformationProcess g_pfnNtQueryInfoProcess = NULL;

// Prototypes des API de version.dll chargées dynamiquement
typedef DWORD (APIENTRY *pfnGetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef BOOL (APIENTRY *pfnGetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL (APIENTRY *pfnVerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

// Prototypes des fonctions
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitListViewColumns(HWND hListView);
void RefreshProcessList(void);
void TerminateSelectedProcessImmediately(void);
void SetSelectedProcessPriority(DWORD priorityClass);
void UpdateStatusBar(int displayedCount, int totalCount, double totalCpu);
void GetProcessCommandLine(HANDLE hProcess, WCHAR *pszCmdLine, DWORD maxChars);
const WCHAR* PriorityClassToString(DWORD priorityClass);
void EnableDebugPrivilege(void);
void UpdateHeaderSortArrow(void);
int ProcessCompare(const void *a, const void *b);
BOOL IsWindowsBaseProcess(DWORD pid, const WCHAR *szExeName, const WCHAR *szFullPath);
BOOL CheckIfMicrosoftSignature(const WCHAR *szFullPath);

// Gestion de settings.ini et des exclusions
void GetSettingsIniPath(WCHAR* szPath, DWORD cchMax);
void LoadSettings(void);
void SaveSettings(void);
BOOL IsProcessExcluded(const WCHAR* szExeName);
BOOL AddExcludedProcess(const WCHAR* szExeName);
BOOL RemoveExcludedProcess(int index);
void ClearAllExcludedProcesses(void);
void UpdateExclusionsButtonText(void);

// Gestion des 3 icônes dynamiques d'historique dans la barre des tâches (Tray Icons)
HICON CreateHistoryGraphIcon(const double *history, int count, DWORD baseColor, DWORD peakColor, DWORD alertColor);
void UpdateTrayIcons(double cpuPct, double memPct, double gpuPct, double usedRamGb, double totalRamGb);
void RecreateTrayIcons(BOOL bResetCreatedFlags);
void RemoveAllTrayIcons(void);

// Télémétrie GPU réelle via PDH ("\GPU Engine(*)\Utilization Percentage")
void InitGpuTelemetry(void);
void ShutdownGpuTelemetry(void);
void CollectGpuTelemetry(ULONGLONG curTick);
double LookupCachedGpuPercent(DWORD pid);

// ============================================================================
// Centralisation des chaînes de caractères (Chargement depuis resource.rc)
// ============================================================================
#define RES_BUF_COUNT 16
#define RES_BUF_LEN 256

int GetEffectiveLanguage(void) {
    if (g_nLanguageSetting >= APP_LANG_EN && g_nLanguageSetting <= APP_LANG_PT) {
        return g_nLanguageSetting;
    }

    // Mode Auto : langue UI du système Windows, repli sur l'anglais si non supportée
    LANGID langId = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(langId)) {
    case LANG_FRENCH:     return APP_LANG_FR;
    case LANG_SPANISH:    return APP_LANG_ES;
    case LANG_GERMAN:     return APP_LANG_DE;
    case LANG_ITALIAN:    return APP_LANG_IT;
    case LANG_PORTUGUESE: return APP_LANG_PT;
    default:              return APP_LANG_EN;
    }
}

const WCHAR* GetResString(UINT uID) {
    static WCHAR s_bufs[RES_BUF_COUNT][RES_BUF_LEN];
    static int s_bufIdx = 0;

    int idx = s_bufIdx;
    s_bufIdx = (s_bufIdx + 1) % RES_BUF_COUNT;

    int effLang = GetEffectiveLanguage();
    UINT targetID = uID;
    switch (effLang) {
    case APP_LANG_EN: targetID = EN_ID(uID); break;
    case APP_LANG_ES: targetID = ES_ID(uID); break;
    case APP_LANG_DE: targetID = DE_ID(uID); break;
    case APP_LANG_IT: targetID = IT_ID(uID); break;
    case APP_LANG_PT: targetID = PT_ID(uID); break;
    default:          targetID = uID;        break;
    }

    if (LoadStringW(GetModuleHandleW(NULL), targetID, s_bufs[idx], RES_BUF_LEN) > 0) {
        return s_bufs[idx];
    }

    // Valeur de secours (fallback) si l'exécutable est compilé sans .rc lié
    BOOL bEn = (effLang != APP_LANG_FR);
    switch (uID) {
    case IDS_APP_TITLE: return bEn ? L"Windows Task Manager (Native C Win32)" : L"Gestionnaire des tâches Windows (C Win32 Natif)";
    case IDS_COL_NAME: return bEn ? L"Image Name" : L"Nom de l'image";
    case IDS_COL_PID: return L"PID";
    case IDS_COL_CPU: return L"CPU %";
    case IDS_COL_RAM: return bEn ? L"Memory (MB)" : L"Mémoire (Mo)";
    case IDS_COL_DISK: return bEn ? L"Disk (MB/s)" : L"Disque (Mo/s)";
    case IDS_COL_NET: return bEn ? L"Network (KB/s)" : L"Réseau (Ko/s)";
    case IDS_COL_GPU: return L"GPU %";
    case IDS_COL_PRIORITY: return bEn ? L"Priority" : L"Priorité";
    case IDS_COL_CMDLINE: return bEn ? L"Command Line (CLI Args)" : L"Ligne de commande (CLI Args)";
    case IDS_BTN_AUTOREFRESH_ON: return bEn ? L"Auto-refresh On" : L"Auto-refresh Oui";
    case IDS_BTN_AUTOREFRESH_OFF: return bEn ? L"Auto-refresh Off" : L"Auto-refresh Non";
    case IDS_BTN_REFRESH: return bEn ? L"Refresh (F5)" : L"Actualiser (F5)";
    case IDS_BTN_WINDOWS_ON: return bEn ? L"Windows On" : L"Windows Oui";
    case IDS_BTN_WINDOWS_OFF: return bEn ? L"Windows Off" : L"Windows Non";
    case IDS_BTN_EXCLUSIONS_FMT: return L"Exclusions (%d)";
    case IDS_LABEL_FILTER: return bEn ? L"Filter:" : L"Filtrer :";
    case IDS_BTN_SETTINGS: return bEn ? L"Settings" : L"Paramètres";
    case IDS_BTN_ABOUT: return bEn ? L"About" : L"À propos";
    case IDS_MENU_KILL_IMMEDIATE: return bEn ? L"Kill immediately\tDel" : L"Tuer immédiatement\tSuppr";
    case IDS_MENU_PRIORITY_REALTIME: return bEn ? L"Realtime" : L"Temps réel";
    case IDS_MENU_PRIORITY_HIGH: return bEn ? L"High" : L"Haute";
    case IDS_MENU_PRIORITY_ABOVENORM: return bEn ? L"Above normal" : L"Supérieure à la normale";
    case IDS_MENU_PRIORITY_NORMAL: return bEn ? L"Normal" : L"Normale";
    case IDS_MENU_PRIORITY_BELOWNORM: return bEn ? L"Below normal" : L"Inférieure à la normale";
    case IDS_MENU_PRIORITY_IDLE: return bEn ? L"Low (Idle)" : L"Basse (Idle)";
    case IDS_MENU_SET_PRIORITY: return bEn ? L"Set priority" : L"Définir la priorité";
    case IDS_MENU_COPY_CMDLINE: return bEn ? L"Copy command line (CLI)" : L"Copier la ligne de commande (CLI)";
    case IDS_MENU_OPEN_LOCATION: return bEn ? L"Open file location" : L"Ouvrir l'emplacement du fichier";
    case IDS_MENU_HIDE_BY_NAME_FMT: return bEn ? L"Hide \"%s\" by name" : L"Masquer \"%s\" par son nom";
    case IDS_MENU_MANAGE_EXCLUSIONS: return bEn ? L"Manage excluded processes..." : L"Gérer les processus exclus...";
    case IDS_MENU_NO_EXCLUSIONS: return bEn ? L"(No excluded processes)" : L"(Aucun processus exclu par son nom)";
    case IDS_MENU_RESTORE_HEADER: return bEn ? L"--- Click to restore ---" : L"--- Cliquer pour réafficher ---";
    case IDS_MENU_RESTORE_ITEM_FMT: return bEn ? L"Restore: %s" : L"Réafficher : %s";
    case IDS_MENU_CLEAR_EXCLUSIONS: return bEn ? L"Restore all (clear exclusions)" : L"Tout réafficher (effacer les exclusions)";
    case IDS_TRAY_OPEN: return bEn ? L"Open Task Manager" : L"Ouvrir le Task Manager";
    case IDS_TRAY_EXIT: return bEn ? L"Exit permanently" : L"Quitter définitivement";
    case IDS_TRAY_CPU_FMT: return bEn ? L"CPU History: %.1f %%" : L"Historique CPU : %.1f %%";
    case IDS_TRAY_RAM_FMT: return bEn ? L"RAM History: %.1f %% (%.1f / %.1f GB)" : L"Historique RAM : %.1f %% (%.1f / %.1f Go)";
    case IDS_TRAY_GPU_FMT: return bEn ? L"GPU History: %.1f %%" : L"Historique GPU : %.1f %%";
    case IDS_TRAY_TIP_RAM_FMT: return bEn ? L"RAM: %.1f %% (%.1f/%.1fGB)\n1. %.10s: %uMB\n2. %.10s: %uMB\n3. %.10s: %uMB\n4. %.10s: %uMB" : L"RAM : %.1f %% (%.1f/%.1fGo)\n1. %.10s: %uMo\n2. %.10s: %uMo\n3. %.10s: %uMo\n4. %.10s: %uMo";
    case IDS_STATUS_PROCESSES_FMT: return bEn ? L"Processes: %d / %d" : L"Processus : %d / %d";
    case IDS_STATUS_CPU_FMT: return bEn ? L"Total CPU: %.1f %%" : L"CPU Global : %.1f %%";
    case IDS_STATUS_RAM_FMT: return bEn ? L"RAM: %u / %u MB (%u %%)" : L"RAM : %u / %u Mo (%u %%)";
    case IDS_STATUS_EXCLUSIONS_FMT: return bEn ? L"Exclusions: %d active | Sort: %s %s" : L"Exclusions : %d actives | Tri : %s %s";
    case IDS_GRAPH_CPU_TITLE: return L"CPU";
    case IDS_GRAPH_CPU_SUB: return bEn ? L"1 hour • % utilization" : L"1 heure • % d'utilisation";
    case IDS_GRAPH_RAM_TITLE: return bEn ? L"Memory" : L"Mémoire";
    case IDS_GRAPH_RAM_SUB_FMT: return bEn ? L"%.1f / %.1f GB (1h)" : L"%.1f / %.1f Go (1h)";
    case IDS_GRAPH_RAM_SUB_DEFAULT: return bEn ? L"Memory usage (1h)" : L"Utilisation mémoire (1h)";
    case IDS_GRAPH_GPU_TITLE: return L"GPU";
    case IDS_GRAPH_GPU_SUB: return bEn ? L"3D Engine • 1 hour" : L"Moteur 3D • 1 heure";
    case IDS_TIP_TIME_MIN_SEC: return L"-%dm %02ds • %.1f %%";
    case IDS_TIP_TIME_SEC: return L"-%ds • %.1f %%";
    case IDS_TIP_TOP_PROC: return bEn ? L"Top: %.18s (%u)" : L"Top : %.18s (%u)";
    case IDS_TIP_USAGE_PCT: return bEn ? L"Usage: %.1f %%" : L"Utilisation : %.1f %%";
    case IDS_TIP_USAGE_RAM: return bEn ? L"Memory: %u MB" : L"Mémoire : %u Mo";
    case IDS_TIP_NO_DATA: return bEn ? L"No activity" : L"Aucune activité";
    case IDS_PRIO_REALTIME: return bEn ? L"Realtime" : L"Temps réel";
    case IDS_PRIO_HIGH: return bEn ? L"High" : L"Haute";
    case IDS_PRIO_ABOVENORM: return bEn ? L"Above normal" : L"Supérieure";
    case IDS_PRIO_NORMAL: return bEn ? L"Normal" : L"Normale";
    case IDS_PRIO_BELOWNORM: return bEn ? L"Below normal" : L"Inférieure";
    case IDS_PRIO_IDLE: return bEn ? L"Low" : L"Basse";
    case IDS_PRIO_UNKNOWN: return bEn ? L"Unknown" : L"Inconnue";
    case IDS_STR_RESTRICTED: return bEn ? L"[System / Restricted access]" : L"[Système / Accès restreint]";
    case IDS_STR_RESTRICTED_ACCESS: return bEn ? L"[Restricted access]" : L"[Accès restreint]";
    case IDS_STR_NO_NTAPI: return bEn ? L"[Unavailable - NTAPI missing]" : L"[Non disponible - NTAPI absente]";
    case IDS_UNIT_RAM_FMT: return bEn ? L"%u MB" : L"%u Mo";
    case IDS_UNIT_DISK_FMT: return bEn ? L"%.2f MB/s" : L"%.2f Mo/s";
    case IDS_UNIT_NET_FMT: return bEn ? L"%.1f KB/s" : L"%.1f Ko/s";
    case IDS_SETTINGS_TITLE: return bEn ? L"Settings" : L"Paramètres";
    case IDS_SETTINGS_LANG_LABEL: return bEn ? L"Interface language:" : L"Langue de l'interface :";
    case IDS_SETTINGS_LANG_AUTO: return bEn ? L"System (Auto - English fallback)" : L"Système (Auto - Anglais par défaut)";
    case IDS_SETTINGS_LANG_EN: return L"English";
    case IDS_SETTINGS_LANG_FR: return L"Français";
    case IDS_SETTINGS_LANG_ES: return L"Español";
    case IDS_SETTINGS_LANG_DE: return L"Deutsch";
    case IDS_SETTINGS_LANG_IT: return L"Italiano";
    case IDS_SETTINGS_LANG_PT: return L"Português";
    case IDS_SETTINGS_AUTOREFRESH: return bEn ? L"Auto-refresh (1 second)" : L"Rafraîchissement automatique (1 seconde)";
    case IDS_SETTINGS_HIDE_WINDOWS: return bEn ? L"Hide core Windows processes" : L"Masquer les processus Windows de base";
    case IDS_SETTINGS_ACTIVE_LANG_FMT: return bEn ? L"Active language: %s" : L"Langue active : %s";
    case IDS_BTN_OK: return L"OK";
    case IDS_BTN_CANCEL: return bEn ? L"Cancel" : L"Annuler";
    case IDS_ABOUT_TITLE: return bEn ? L"About" : L"À propos";
    case IDS_ABOUT_APP_NAME: return bEn ? L"Windows Task Manager" : L"Gestionnaire des tâches Windows";
    case IDS_ABOUT_VERSION_FMT: return L"Version %s (Build %d)";
    case IDS_ABOUT_COMPILE_FMT: return bEn ? L"Built on %s at %s" : L"Compilé le %s à %s";
    case IDS_ABOUT_TECH: return bEn ? L"Native Win32 API / NTAPI in pure C (MinGW-w64)" : L"Win32 API / NTAPI natif en C pur (MinGW-w64)";
    case IDS_BTN_CLOSE: return bEn ? L"Close" : L"Fermer";
    default: return L"";
    }
}

const WCHAR* GetColumnTitle(int col) {
    switch (col) {
    case COL_NAME: return GetResString(IDS_COL_NAME);
    case COL_PID: return GetResString(IDS_COL_PID);
    case COL_CPU: return GetResString(IDS_COL_CPU);
    case COL_RAM: return GetResString(IDS_COL_RAM);
    case COL_DISK: return GetResString(IDS_COL_DISK);
    case COL_NET: return GetResString(IDS_COL_NET);
    case COL_GPU: return GetResString(IDS_COL_GPU);
    case COL_PRIORITY: return GetResString(IDS_COL_PRIORITY);
    case COL_CMDLINE: return GetResString(IDS_COL_CMDLINE);
    default: return L"";
    }
}

// Recherche sous-chaîne insensible à la casse autonome
static BOOL ContainsSubstringI(const WCHAR *haystack, const WCHAR *needle) {
    if (!haystack || !needle) return FALSE;
    if (!*needle) return TRUE;
    for (; *haystack; haystack++) {
        const WCHAR *h = haystack;
        const WCHAR *n = needle;
        while (*h && *n && (towlower(*h) == towlower(*n))) {
            h++;
            n++;
        }
        if (!*n) return TRUE;
    }
    return FALSE;
}

// Obtention du chemin absolu du fichier settings.ini dans le dossier de l'exécutable
void GetSettingsIniPath(WCHAR* szPath, DWORD cchMax) {
    GetModuleFileNameW(NULL, szPath, cchMax);
    WCHAR* pLastSlash = wcsrchr(szPath, L'\\');
    if (pLastSlash) {
        *(pLastSlash + 1) = L'\0';
        wcscat_s(szPath, cchMax, L"settings.ini");
    } else {
        wcscpy_s(szPath, cchMax, L".\\settings.ini");
    }
}

// Chargement des réglages et des exclusions depuis settings.ini
void LoadSettings(void) {
    WCHAR szIniPath[MAX_PATH];
    GetSettingsIniPath(szIniPath, MAX_PATH);

    if (GetFileAttributesW(szIniPath) == INVALID_FILE_ATTRIBUTES) {
        return; // Le fichier n'existe pas encore, on conserve les valeurs par défaut
    }

    g_bAutoRefresh = (GetPrivateProfileIntW(L"Settings", L"AutoRefresh", 1, szIniPath) != 0);
    g_bHideWindowsProcesses = (GetPrivateProfileIntW(L"Settings", L"HideWindowsProcesses", 0, szIniPath) != 0);

    WCHAR szLang[32] = { 0 };
    GetPrivateProfileStringW(L"Settings", L"Language", L"auto", szLang, 32, szIniPath);
    if (_wcsicmp(szLang, L"fr") == 0) {
        g_nLanguageSetting = APP_LANG_FR;
    } else if (_wcsicmp(szLang, L"en") == 0) {
        g_nLanguageSetting = APP_LANG_EN;
    } else if (_wcsicmp(szLang, L"es") == 0) {
        g_nLanguageSetting = APP_LANG_ES;
    } else if (_wcsicmp(szLang, L"de") == 0) {
        g_nLanguageSetting = APP_LANG_DE;
    } else if (_wcsicmp(szLang, L"it") == 0) {
        g_nLanguageSetting = APP_LANG_IT;
    } else if (_wcsicmp(szLang, L"pt") == 0) {
        g_nLanguageSetting = APP_LANG_PT;
    } else {
        g_nLanguageSetting = APP_LANG_AUTO;
    }

    int savedSortCol = GetPrivateProfileIntW(L"Settings", L"SortColumn", COL_CPU, szIniPath);
    if (savedSortCol >= 0 && savedSortCol < COL_COUNT) {
        g_SortColumn = savedSortCol;
    }
    g_bSortAscending = (GetPrivateProfileIntW(L"Settings", L"SortAscending", 0, szIniPath) != 0);

    g_ExcludedCount = GetPrivateProfileIntW(L"Exclusions", L"Count", 0, szIniPath);
    if (g_ExcludedCount < 0) g_ExcludedCount = 0;
    if (g_ExcludedCount > MAX_EXCLUDED_PROCESSES) g_ExcludedCount = MAX_EXCLUDED_PROCESSES;

    for (int i = 0; i < g_ExcludedCount; i++) {
        WCHAR szKey[32];
        wsprintfW(szKey, L"Process%d", i);
        GetPrivateProfileStringW(L"Exclusions", szKey, L"", g_ExcludedProcesses[i], MAX_PATH, szIniPath);
    }
}

// Enregistrement des réglages et des exclusions dans settings.ini
void SaveSettings(void) {
    WCHAR szIniPath[MAX_PATH];
    GetSettingsIniPath(szIniPath, MAX_PATH);

    WritePrivateProfileStringW(L"Settings", L"AutoRefresh", g_bAutoRefresh ? L"1" : L"0", szIniPath);
    WritePrivateProfileStringW(L"Settings", L"HideWindowsProcesses", g_bHideWindowsProcesses ? L"1" : L"0", szIniPath);

    const WCHAR* szLangVal = L"auto";
    switch (g_nLanguageSetting) {
    case APP_LANG_FR: szLangVal = L"fr"; break;
    case APP_LANG_EN: szLangVal = L"en"; break;
    case APP_LANG_ES: szLangVal = L"es"; break;
    case APP_LANG_DE: szLangVal = L"de"; break;
    case APP_LANG_IT: szLangVal = L"it"; break;
    case APP_LANG_PT: szLangVal = L"pt"; break;
    default:          szLangVal = L"auto"; break;
    }
    WritePrivateProfileStringW(L"Settings", L"Language", szLangVal, szIniPath);

    WCHAR szBuf[32];
    wsprintfW(szBuf, L"%d", g_SortColumn);
    WritePrivateProfileStringW(L"Settings", L"SortColumn", szBuf, szIniPath);

    WritePrivateProfileStringW(L"Settings", L"SortAscending", g_bSortAscending ? L"1" : L"0", szIniPath);

    // Effacement de la section [Exclusions] pour supprimer d'éventuelles clés obsolètes
    WritePrivateProfileStringW(L"Exclusions", NULL, NULL, szIniPath);

    wsprintfW(szBuf, L"%d", g_ExcludedCount);
    WritePrivateProfileStringW(L"Exclusions", L"Count", szBuf, szIniPath);

    for (int i = 0; i < g_ExcludedCount; i++) {
        WCHAR szKey[32];
        wsprintfW(szKey, L"Process%d", i);
        WritePrivateProfileStringW(L"Exclusions", szKey, g_ExcludedProcesses[i], szIniPath);
    }

    // Vidage du cache INI sur le disque
    WritePrivateProfileStringW(NULL, NULL, NULL, szIniPath);
}

// Vérifie si un processus est dans la liste des exclusions
BOOL IsProcessExcluded(const WCHAR* szExeName) {
    if (!szExeName || szExeName[0] == L'\0') return FALSE;
    for (int i = 0; i < g_ExcludedCount; i++) {
        if (_wcsicmp(g_ExcludedProcesses[i], szExeName) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

// Ajoute un processus à la liste des exclusions par son nom
BOOL AddExcludedProcess(const WCHAR* szExeName) {
    if (!szExeName || szExeName[0] == L'\0') return FALSE;
    if (IsProcessExcluded(szExeName)) return FALSE;
    if (g_ExcludedCount >= MAX_EXCLUDED_PROCESSES) return FALSE;

    wcsncpy_s(g_ExcludedProcesses[g_ExcludedCount], MAX_PATH, szExeName, _TRUNCATE);
    g_ExcludedCount++;
    return TRUE;
}

// Retire un processus de la liste des exclusions
BOOL RemoveExcludedProcess(int index) {
    if (index < 0 || index >= g_ExcludedCount) return FALSE;
    for (int i = index; i < g_ExcludedCount - 1; i++) {
        wcsncpy_s(g_ExcludedProcesses[i], MAX_PATH, g_ExcludedProcesses[i + 1], _TRUNCATE);
    }
    g_ExcludedCount--;
    return TRUE;
}

// Réinitialise toutes les exclusions
void ClearAllExcludedProcesses(void) {
    g_ExcludedCount = 0;
}

// Mise à jour du libellé du bouton Exclusions
void UpdateExclusionsButtonText(void) {
    if (g_hBtnExclusions) {
        WCHAR szText[64];
        wsprintfW(szText, GetResString(IDS_BTN_EXCLUSIONS_FMT), g_ExcludedCount);
        SetWindowTextW(g_hBtnExclusions, szText);
    }
}

// Création d'une icône 16x16 affichant l'historique d'utilisation sous forme de sparkline / graphique en barres
HICON CreateHistoryGraphIcon(const double *history, int count, DWORD baseColor, DWORD peakColor, DWORD alertColor) {
    DWORD pixels[16 * 16];

    // Fond sombre ardoise moderne avec bordure extérieure nette
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            if (x == 0 || x == 15 || y == 0 || y == 15) {
                pixels[y * 16 + x] = 0xFF1E293B; // Bordure extérieure gris-bleu 1px
            } else if (y == 7) {
                pixels[y * 16 + x] = 0xFF172235; // Ligne guide médiane 50%
            } else {
                pixels[y * 16 + x] = 0xFF0A0F1D; // Fond sombre intérieur
            }
        }
    }

    // Rendu des colonnes d'historique (x de 1 à 14 = 14 points temporels)
    // history[0] = point le plus ancien (14s), history[13] = point le plus récent (actuel)
    for (int col = 0; col < 14; col++) {
        int x = 1 + col;
        double val = (col < count) ? history[col] : 0.0;
        if (val < 0.0) val = 0.0;
        if (val > 100.0) val = 100.0;

        // Hauteur de la barre : de 0 à 13 pixels (y de 14 bas jusqu'à 1 haut)
        int h = (int)((val / 100.0) * 13.0 + 0.5);
        if (val > 0.1 && h == 0) h = 1; // Au moins 1 pixel si activité détectée
        if (h > 13) h = 13;

        DWORD colBase = (val > 80.0) ? alertColor : baseColor;
        DWORD colPeak = (val > 80.0) ? 0xFFFCA5A5 : peakColor;

        for (int fromBottom = 0; fromBottom < h; fromBottom++) {
            int y = 14 - fromBottom;
            if (fromBottom == h - 1) {
                pixels[y * 16 + x] = colPeak; // Ligne supérieure lumineuse
            } else {
                pixels[y * 16 + x] = colBase; // Corps de la barre d'historique
            }
        }
    }

    // Masque AND 1-bit monochrome (32 octets = 16 lignes de 2 octets)
    BYTE andMask[16 * 2] = { 0 };

    HBITMAP hbmColor = CreateBitmap(16, 16, 1, 32, pixels);
    HBITMAP hbmMask = CreateBitmap(16, 16, 1, 1, andMask);
    if (!hbmColor || !hbmMask) {
        if (hbmColor) DeleteObject(hbmColor);
        if (hbmMask) DeleteObject(hbmMask);
        return NULL;
    }

    ICONINFO ii;
    memset(&ii, 0, sizeof(ICONINFO));
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    return hIcon;
}

// Création programmatique d'une icône native de Task Manager (cadre sombre + grille + onde télémétrique néon)
// Sert de fallback automatique si l'exécutable est compilé sans le fichier de ressource .rc lié
HICON CreateTaskManagerAppIcon(int size) {
    if (size != 16 && size != 32 && size != 48) size = 32;

    int totalPixels = size * size;
    DWORD *pixels = (DWORD*)malloc(totalPixels * sizeof(DWORD));
    if (!pixels) return NULL;

    int andRowBytes = ((size + 31) / 32) * 4;
    int andSize = andRowBytes * size;
    BYTE *andMask = (BYTE*)calloc(1, andSize);
    if (!andMask) {
        free(pixels);
        return NULL;
    }

    int r = (size <= 16) ? 2 : (size / 7);
    int topBarH = (size <= 16) ? 3 : (size / 7);
    int screenBottom = size - 2;
    int screenTop = topBarH;
    int screenH = screenBottom - screenTop;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int dx = (x < size / 2) ? x : (size - 1 - x);
            int dy = (y < size / 2) ? y : (size - 1 - y);

            // Coins arrondis transparents
            if (dx < r && dy < r) {
                int distSq = (r - dx) * (r - dx) + (r - dy) * (r - dy);
                if (distSq > r * r) {
                    pixels[y * size + x] = 0x00000000;
                    // Marquer le bit dans le masque AND pour transparence Windows native
                    int byteIdx = (size - 1 - y) * andRowBytes + (x / 8);
                    andMask[byteIdx] |= (BYTE)(1 << (7 - (x % 8)));
                    continue;
                }
            }

            // Bordure extérieure de la fenêtre
            if (x == 0 || x == size - 1 || y == 0 || y == size - 1 ||
                (dx < r && dy < r && (r - dx) * (r - dx) + (r - dy) * (r - dy) >= (r - 1) * (r - 1))) {
                pixels[y * size + x] = 0xFF1E293B; // Slate 800
            } else if (y < topBarH) {
                pixels[y * size + x] = 0xFF0F172A; // Barre de titre sombre
            } else {
                // Écran intérieur du moniteur de tâches
                pixels[y * size + x] = 0xFF090D16; // Noir bleuté profond

                // Lignes de quadrillage télémétrique
                int stepY = (size <= 16) ? 4 : (size / 4);
                int stepX = (size <= 16) ? 4 : (size / 4);
                if ((y - screenTop) % stepY == 0 || (x - 1) % stepX == 0) {
                    pixels[y * size + x] = 0xFF122637; // Grille cyan très subtile
                }
            }
        }
    }

    // Points indicateurs LED dans la barre de titre
    if (size >= 16) {
        pixels[1 * size + (size - 3)] = 0xFF10B981; // LED verte active
        if (size >= 32) {
            pixels[1 * size + (size - 5)] = 0xFF06B6D4; // LED cyan
            pixels[1 * size + (size - 7)] = 0xFF3B82F6; // LED bleue
        }
    }

    // Onde d'activité CPU / télémétrie (courbe d'impulsion style Task Manager)
    int countW = size - 2;
    int *waveY = (int*)malloc(countW * sizeof(int));
    if (waveY) {
        for (int i = 0; i < countW; i++) {
            double nx = (double)i / (double)(countW - 1);
            double ny = 0.25;
            if (nx < 0.20) {
                ny = 0.25;
            } else if (nx < 0.30) {
                double t = (nx - 0.20) / 0.10;
                ny = 0.25 - 0.15 * (1.0 - fabs(2.0 * t - 1.0));
            } else if (nx < 0.45) {
                double t = (nx - 0.30) / 0.15;
                if (t < 0.5) {
                    ny = 0.25 + 0.65 * (t / 0.5);
                } else {
                    ny = 0.90 - 0.85 * ((t - 0.5) / 0.5);
                }
            } else if (nx < 0.60) {
                double t = (nx - 0.45) / 0.15;
                if (t < 0.5) {
                    ny = 0.05 + 0.55 * (t / 0.5);
                } else {
                    ny = 0.60 - 0.35 * ((t - 0.5) / 0.5);
                }
            } else if (nx < 0.75) {
                double t = (nx - 0.60) / 0.15;
                ny = 0.25 + 0.20 * (1.0 - fabs(2.0 * t - 1.0));
            } else {
                ny = 0.25;
            }

            int py = (int)(screenBottom - ny * screenH + 0.5);
            if (py < screenTop) py = screenTop;
            if (py > screenBottom) py = screenBottom;
            waveY[i] = py;

            int x = 1 + i;
            // Remplissage sous la courbe (gradient cyan sombre)
            for (int fy = py + 1; fy <= screenBottom; fy++) {
                DWORD cur = pixels[fy * size + x];
                if ((cur & 0xFF000000) != 0) {
                    pixels[fy * size + x] = 0xFF083344;
                }
            }
        }

        // Tracé de l'onde lumineuse néon
        int peakY = size;
        int peakX = 1;
        for (int i = 0; i < countW; i++) {
            int x = 1 + i;
            int y = waveY[i];
            pixels[y * size + x] = 0xFF00F0FF; // Cyan éclatant
            if (y < peakY) {
                peakY = y;
                peakX = x;
            }
            if (size >= 32 && y > screenTop) {
                pixels[(y - 1) * size + x] = 0xFF06B6D4;
            }
        }

        // Pic d'activité blanc vif au sommet de l'impulsion
        pixels[peakY * size + peakX] = 0xFFFFFFFF;
        if (size >= 32 && peakY + 1 <= screenBottom) {
            pixels[(peakY + 1) * size + peakX] = 0xFFA5F3FC;
        }

        free(waveY);
    }

    HBITMAP hbmColor = CreateBitmap(size, size, 1, 32, pixels);
    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, andMask);

    free(pixels);
    free(andMask);

    if (!hbmColor || !hbmMask) {
        if (hbmColor) DeleteObject(hbmColor);
        if (hbmMask) DeleteObject(hbmMask);
        return NULL;
    }

    ICONINFO ii;
    memset(&ii, 0, sizeof(ICONINFO));
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    return hIcon;
}

// Structure pour les statistiques des plus gros consommateurs de ressource
typedef struct {
    WCHAR szName[64];
    double value;
    DWORD pid;
} TopConsumer;

// Récupère les 4 processus les plus gourmands pour une ressource donnée (0: CPU, 1: RAM, 2: GPU)
void GetTop4Consumers(int resourceType, TopConsumer top[4]) {
    for (int i = 0; i < 4; i++) {
        top[i].szName[0] = L'\0';
        top[i].value = -1.0;
        top[i].pid = 0;
    }

    for (int i = 0; i < g_ProcessCount; i++) {
        if (g_Processes[i].pid == 0) continue; // Ignorer System Idle Process

        double val = 0.0;
        if (resourceType == 0) {
            val = g_Processes[i].cpuPercent;
        } else if (resourceType == 1) {
            val = (double)g_Processes[i].workingSetMB;
        } else if (resourceType == 2) {
            val = g_Processes[i].gpuPercent;
        }

        // Tri par insertion directe dans le Top 4
        for (int r = 0; r < 4; r++) {
            if (val > top[r].value) {
                for (int s = 3; s > r; s--) {
                    top[s] = top[s - 1];
                }
                top[r].value = val;
                top[r].pid = g_Processes[i].pid;
                wcsncpy_s(top[r].szName, 64, g_Processes[i].szName, 14);
                break;
            }
        }
    }
}

// Autorise la réception d'un message même si l'application tourne avec des privilèges élevés (UIPI)
static void AllowMessageThroughUIPI(HWND hWnd, UINT uMsg) {
    if (!hWnd || uMsg == 0) return;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return;

    typedef BOOL (WINAPI *pfnChangeWindowMessageFilterEx)(HWND, UINT, DWORD, PVOID);
    pfnChangeWindowMessageFilterEx pChangeFilter =
        (pfnChangeWindowMessageFilterEx)(void*)GetProcAddress(hUser32, "ChangeWindowMessageFilterEx");
    if (pChangeFilter) {
        // MSGFLT_ALLOW = 1
        pChangeFilter(hWnd, uMsg, 1, NULL);
    }
}

// Ajout ou mise à jour sécurisée d'une icône Tray (repli automatique sur NIM_ADD si Explorer a redémarré)
static void UpsertSingleTrayIcon(NOTIFYICONDATAW *pNid, BOOL *pbCreated) {
    if (!*pbCreated) {
        if (Shell_NotifyIconW(NIM_ADD, pNid)) {
            *pbCreated = TRUE;
        }
    } else {
        if (!Shell_NotifyIconW(NIM_MODIFY, pNid)) {
            *pbCreated = FALSE;
            if (Shell_NotifyIconW(NIM_ADD, pNid)) {
                *pbCreated = TRUE;
            }
        }
    }
}

// Synchronisation interne des 3 icônes d'historique (avec mise à jour optionnelle de l'historique)
static void SyncTrayIconsInternal(BOOL bUpdateHistory, double cpuPct, double memPct, double gpuPct, double usedRamGb, double totalRamGb) {
    if (!g_hMainWnd) return;

    if (bUpdateHistory) {
        g_LastCpuPct = cpuPct;
        g_LastMemPct = memPct;
        g_LastGpuPct = gpuPct;
        g_LastUsedRamGb = usedRamGb;
        g_LastTotalRamGb = totalRamGb;

        // Décalage de l'historique vers la gauche et ajout du nouvel échantillon
        if (g_TrayHistoryCount == 0) {
            for (int i = 0; i < TRAY_HISTORY_POINTS; i++) {
                g_TrayCpuHistory[i] = cpuPct;
                g_TrayRamHistory[i] = memPct;
                g_TrayGpuHistory[i] = gpuPct;
            }
            g_TrayHistoryCount = TRAY_HISTORY_POINTS;
        } else {
            for (int i = 0; i < TRAY_HISTORY_POINTS - 1; i++) {
                g_TrayCpuHistory[i] = g_TrayCpuHistory[i + 1];
                g_TrayRamHistory[i] = g_TrayRamHistory[i + 1];
                g_TrayGpuHistory[i] = g_TrayGpuHistory[i + 1];
            }
            g_TrayCpuHistory[TRAY_HISTORY_POINTS - 1] = cpuPct;
            g_TrayRamHistory[TRAY_HISTORY_POINTS - 1] = memPct;
            g_TrayGpuHistory[TRAY_HISTORY_POINTS - 1] = gpuPct;
        }
    }

    // Calcul des Top 4 consommateurs pour chaque ressource
    TopConsumer topCpu[4];
    TopConsumer topRam[4];
    TopConsumer topGpu[4];
    GetTop4Consumers(0, topCpu);
    GetTop4Consumers(1, topRam);
    GetTop4Consumers(2, topGpu);

    // 1. Icône CPU : Vert émeraude (Base: #10B981, Pic: #34D399, Alerte: #EF4444)
    // Au survol souris : affiche le total CPU et le Top 4 des processus les plus gourmands en CPU
    HICON hNewCpu = CreateHistoryGraphIcon(g_TrayCpuHistory, TRAY_HISTORY_POINTS, 0xFF10B981, 0xFF34D399, 0xFFEF4444);
    if (hNewCpu) {
        NOTIFYICONDATAW nid;
        memset(&nid, 0, sizeof(NOTIFYICONDATAW));
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = g_hMainWnd;
        nid.uID = TRAY_ID_CPU;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = hNewCpu;

        swprintf_s(nid.szTip, sizeof(nid.szTip) / sizeof(WCHAR),
            L"CPU : %.1f %%\n1. %.11s: %.1f%%\n2. %.11s: %.1f%%\n3. %.11s: %.1f%%\n4. %.11s: %.1f%%",
            g_LastCpuPct,
            topCpu[0].szName[0] ? topCpu[0].szName : L"-", (topCpu[0].value > 0.0) ? topCpu[0].value : 0.0,
            topCpu[1].szName[0] ? topCpu[1].szName : L"-", (topCpu[1].value > 0.0) ? topCpu[1].value : 0.0,
            topCpu[2].szName[0] ? topCpu[2].szName : L"-", (topCpu[2].value > 0.0) ? topCpu[2].value : 0.0,
            topCpu[3].szName[0] ? topCpu[3].szName : L"-", (topCpu[3].value > 0.0) ? topCpu[3].value : 0.0);

        UpsertSingleTrayIcon(&nid, &g_bTrayCpuCreated);

        if (g_hIconCpu) DestroyIcon(g_hIconCpu);
        g_hIconCpu = hNewCpu;
    }

    // 2. Icône RAM : Violet vif (Base: #8B5CF6, Pic: #A78BFA, Alerte: #EF4444)
    // Au survol souris : affiche le total RAM et le Top 4 des processus les plus gourmands en mémoire
    HICON hNewRam = CreateHistoryGraphIcon(g_TrayRamHistory, TRAY_HISTORY_POINTS, 0xFF8B5CF6, 0xFFA78BFA, 0xFFEF4444);
    if (hNewRam) {
        NOTIFYICONDATAW nid;
        memset(&nid, 0, sizeof(NOTIFYICONDATAW));
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = g_hMainWnd;
        nid.uID = TRAY_ID_RAM;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = hNewRam;

        swprintf_s(nid.szTip, sizeof(nid.szTip) / sizeof(WCHAR),
            GetResString(IDS_TRAY_TIP_RAM_FMT),
            g_LastMemPct, g_LastUsedRamGb, g_LastTotalRamGb,
            topRam[0].szName[0] ? topRam[0].szName : L"-", (UINT)(topRam[0].value > 0.0 ? topRam[0].value : 0.0),
            topRam[1].szName[0] ? topRam[1].szName : L"-", (UINT)(topRam[1].value > 0.0 ? topRam[1].value : 0.0),
            topRam[2].szName[0] ? topRam[2].szName : L"-", (UINT)(topRam[2].value > 0.0 ? topRam[2].value : 0.0),
            topRam[3].szName[0] ? topRam[3].szName : L"-", (UINT)(topRam[3].value > 0.0 ? topRam[3].value : 0.0));

        UpsertSingleTrayIcon(&nid, &g_bTrayRamCreated);

        if (g_hIconRam) DestroyIcon(g_hIconRam);
        g_hIconRam = hNewRam;
    }

    // 3. Icône GPU : Cyan électrique (Base: #06B6D4, Pic: #67E8F9, Alerte: #EF4444)
    // Au survol souris : affiche le total GPU et le Top 4 des processus les plus gourmands en GPU
    HICON hNewGpu = CreateHistoryGraphIcon(g_TrayGpuHistory, TRAY_HISTORY_POINTS, 0xFF06B6D4, 0xFF67E8F9, 0xFFEF4444);
    if (hNewGpu) {
        NOTIFYICONDATAW nid;
        memset(&nid, 0, sizeof(NOTIFYICONDATAW));
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = g_hMainWnd;
        nid.uID = TRAY_ID_GPU;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = hNewGpu;

        swprintf_s(nid.szTip, sizeof(nid.szTip) / sizeof(WCHAR),
            L"GPU : %.1f %%\n1. %.11s: %.1f%%\n2. %.11s: %.1f%%\n3. %.11s: %.1f%%\n4. %.11s: %.1f%%",
            g_LastGpuPct,
            topGpu[0].szName[0] ? topGpu[0].szName : L"-", (topGpu[0].value > 0.0) ? topGpu[0].value : 0.0,
            topGpu[1].szName[0] ? topGpu[1].szName : L"-", (topGpu[1].value > 0.0) ? topGpu[1].value : 0.0,
            topGpu[2].szName[0] ? topGpu[2].szName : L"-", (topGpu[2].value > 0.0) ? topGpu[2].value : 0.0,
            topGpu[3].szName[0] ? topGpu[3].szName : L"-", (topGpu[3].value > 0.0) ? topGpu[3].value : 0.0);

        UpsertSingleTrayIcon(&nid, &g_bTrayGpuCreated);

        if (g_hIconGpu) DestroyIcon(g_hIconGpu);
        g_hIconGpu = hNewGpu;
    }

    if (g_bTrayCpuCreated && g_bTrayRamCreated && g_bTrayGpuCreated) {
        KillTimer(g_hMainWnd, IDT_TRAY_RETRY_TIMER);
    }
}

// Mise à jour des 3 icônes distinctes d'historique (CPU, RAM, GPU) dans la barre des tâches
// avec affichage dynamique des 4 premiers consommateurs au survol de la souris
void UpdateTrayIcons(double cpuPct, double memPct, double gpuPct, double usedRamGb, double totalRamGb) {
    SyncTrayIconsInternal(TRUE, cpuPct, memPct, gpuPct, usedRamGb, totalRamGb);
}

// Recréation des icônes Tray suite au redémarrage d'explorer.exe (TaskbarCreated)
void RecreateTrayIcons(BOOL bResetCreatedFlags) {
    if (!g_hMainWnd) return;

    if (bResetCreatedFlags) {
        g_bTrayCpuCreated = FALSE;
        g_bTrayRamCreated = FALSE;
        g_bTrayGpuCreated = FALSE;
    }

    SyncTrayIconsInternal(FALSE, g_LastCpuPct, g_LastMemPct, g_LastGpuPct, g_LastUsedRamGb, g_LastTotalRamGb);

    if (!g_bTrayCpuCreated || !g_bTrayRamCreated || !g_bTrayGpuCreated) {
        SetTimer(g_hMainWnd, IDT_TRAY_RETRY_TIMER, 1000, NULL);
    } else {
        KillTimer(g_hMainWnd, IDT_TRAY_RETRY_TIMER);
    }
}

// Suppression propre des 3 icônes de la barre des tâches
void RemoveAllTrayIcons(void) {
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(NOTIFYICONDATAW));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hMainWnd;

    if (g_bTrayCpuCreated) {
        nid.uID = TRAY_ID_CPU;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_bTrayCpuCreated = FALSE;
    }
    if (g_hIconCpu) {
        DestroyIcon(g_hIconCpu);
        g_hIconCpu = NULL;
    }

    if (g_bTrayRamCreated) {
        nid.uID = TRAY_ID_RAM;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_bTrayRamCreated = FALSE;
    }
    if (g_hIconRam) {
        DestroyIcon(g_hIconRam);
        g_hIconRam = NULL;
    }

    if (g_bTrayGpuCreated) {
        nid.uID = TRAY_ID_GPU;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_bTrayGpuCreated = FALSE;
    }
    if (g_hIconGpu) {
        DestroyIcon(g_hIconGpu);
        g_hIconGpu = NULL;
    }
}

// Vérification de signature / métadonnées Microsoft dans les ressources PE (chargement dynamique sans -lversion)
BOOL CheckIfMicrosoftSignature(const WCHAR *szFullPath) {
    if (!szFullPath || szFullPath[0] == L'\0') return FALSE;

    HMODULE hVer = GetModuleHandleW(L"version.dll");
    if (!hVer) hVer = LoadLibraryW(L"version.dll");
    if (!hVer) return FALSE;

    pfnGetFileVersionInfoSizeW pGetSize = (pfnGetFileVersionInfoSizeW)(void*)GetProcAddress(hVer, "GetFileVersionInfoSizeW");
    pfnGetFileVersionInfoW pGetInfo = (pfnGetFileVersionInfoW)(void*)GetProcAddress(hVer, "GetFileVersionInfoW");
    pfnVerQueryValueW pQuery = (pfnVerQueryValueW)(void*)GetProcAddress(hVer, "VerQueryValueW");

    if (!pGetSize || !pGetInfo || !pQuery) return FALSE;

    DWORD dummy = 0;
    DWORD size = pGetSize(szFullPath, &dummy);
    if (size == 0) return FALSE;

    LPVOID pData = malloc(size);
    if (!pData) return FALSE;

    BOOL isMicrosoft = FALSE;
    if (pGetInfo(szFullPath, 0, size, pData)) {
        struct LANGANDCODEPAGE {
            WORD wLanguage;
            WORD wCodePage;
        } *lpTranslate;
        UINT cbTranslate = 0;

        if (pQuery(pData, L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate) && cbTranslate >= sizeof(struct LANGANDCODEPAGE)) {
            WCHAR subBlock[128];
            wsprintfW(subBlock, L"\\StringFileInfo\\%04x%04x\\CompanyName", lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
            LPWSTR lpCompany = NULL;
            UINT companyLen = 0;
            if (pQuery(pData, subBlock, (LPVOID*)&lpCompany, &companyLen) && lpCompany && companyLen > 0) {
                if (ContainsSubstringI(lpCompany, L"Microsoft")) {
                    isMicrosoft = TRUE;
                }
            }
        }
    }

    free(pData);
    return isMicrosoft;
}

// Détection robuste des processus système ou de base de Windows
BOOL IsWindowsBaseProcess(DWORD pid, const WCHAR *szExeName, const WCHAR *szFullPath) {
    if (pid == 0 || pid == 4) return TRUE; // Idle et System

    static const WCHAR* s_WindowsBinaries[] = {
        L"smss.exe", L"csrss.exe", L"wininit.exe", L"services.exe",
        L"lsass.exe", L"svchost.exe", L"dwm.exe", L"explorer.exe",
        L"fontdrvhost.exe", L"sihost.exe", L"taskhostw.exe", L"RuntimeBroker.exe",
        L"SearchHost.exe", L"StartMenuExperienceHost.exe", L"ShellExperienceHost.exe",
        L"SecurityHealthSystray.exe", L"SecurityHealthService.exe", L"ctfmon.exe",
        L"spoolsv.exe", L"conhost.exe", L"audiodg.exe", L"SearchIndexer.exe",
        L"smartscreen.exe", L"winlogon.exe", L"SystemSettings.exe", L"TextInputHost.exe",
        L"dllhost.exe", L"WmiPrvSE.exe", L"LockApp.exe"
    };

    for (size_t i = 0; i < sizeof(s_WindowsBinaries) / sizeof(s_WindowsBinaries[0]); i++) {
        if (_wcsicmp(szExeName, s_WindowsBinaries[i]) == 0) {
            return TRUE;
        }
    }

    if (szFullPath && szFullPath[0] != L'\0') {
        WCHAR szWinDir[MAX_PATH];
        if (GetWindowsDirectoryW(szWinDir, MAX_PATH)) {
            if (_wcsnicmp(szFullPath, szWinDir, wcslen(szWinDir)) == 0) {
                if (CheckIfMicrosoftSignature(szFullPath)) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

// Élévation de privilèges SeDebugPrivilege pour accéder aux processus système
void EnableDebugPrivilege(void) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
    }
}

// Conversion FILETIME -> UInt64 (100-nanosecond intervals)
static inline ULONGLONG FileTimeToUInt64(const FILETIME *pft) {
    ULARGE_INTEGER uli;
    uli.LowPart = pft->dwLowDateTime;
    uli.HighPart = pft->dwHighDateTime;
    return uli.QuadPart;
}

// Recherche ou création d'une entrée dans l'historique
static ProcessHistory* FindOrCreateHistory(DWORD pid) {
    for (int i = 0; i < g_HistoryCount; i++) {
        if (g_History[i].pid == pid) {
            return &g_History[i];
        }
    }
    if (g_HistoryCount < MAX_HISTORY_PROCESSES) {
        ProcessHistory *h = &g_History[g_HistoryCount++];
        memset(h, 0, sizeof(ProcessHistory));
        h->pid = pid;
        return h;
    }
    return NULL;
}

// Nettoyage de l'historique pour les processus terminés
static void PruneHistory(const DWORD *activePids, int count) {
    int i = 0;
    while (i < g_HistoryCount) {
        BOOL found = FALSE;
        for (int j = 0; j < count; j++) {
            if (activePids[j] == g_History[i].pid) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            g_History[i] = g_History[g_HistoryCount - 1];
            g_HistoryCount--;
        } else {
            i++;
        }
    }
}

// ============================================================================
// Gestion du thème visuel moderne Windows (Support Dark Mode complet & Explorer)
// ============================================================================

// Variables et pinceaux GDI pour le mode sombre
static BOOL g_bDarkModeActive = FALSE;
static HBRUSH g_hDarkWndBrush = NULL;
static HBRUSH g_hDarkListBrush = NULL;
static HBRUSH g_hDarkHeaderBrush = NULL;
static HBRUSH g_hDarkHeaderHotBrush = NULL;
static HBRUSH g_hDarkHeaderPressBrush = NULL;
static HBRUSH g_hDarkEditBrush = NULL;
static HPEN   g_hDarkBorderPen = NULL;

// Types et pointeurs pour les API UxTheme Dark Mode (Windows 10 1809+ et Windows 11)
typedef enum {
    PAM_DEFAULT = 0,
    PAM_ALLOW_DARK = 1,
    PAM_FORCE_DARK = 2,
    PAM_FORCE_LIGHT = 3,
    PAM_MAX = 4
} PreferredAppMode;

typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND hWnd, BOOL allow);
typedef PreferredAppMode (WINAPI *pfnSetPreferredAppMode)(PreferredAppMode appMode);
typedef BOOL (WINAPI *pfnAllowDarkModeForApp)(BOOL allow); // Windows 10 1809
typedef void (WINAPI *pfnRefreshImmersiveColorPolicyState)(void);
typedef void (WINAPI *pfnFlushMenuThemes)(void);

static pfnAllowDarkModeForWindow g_pfnAllowDarkModeForWindow = NULL;
static pfnSetPreferredAppMode g_pfnSetPreferredAppMode = NULL;
static pfnAllowDarkModeForApp g_pfnAllowDarkModeForApp = NULL;
static pfnRefreshImmersiveColorPolicyState g_pfnRefreshImmersiveColorPolicyState = NULL;
static pfnFlushMenuThemes g_pfnFlushMenuThemes = NULL;

// Détection du mode sombre Windows via le registre système (Windows 10/11)
static BOOL IsWindowsDarkModeActive(void) {
    HKEY hKey;
    DWORD val = 1;
    DWORD size = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &size);
        RegCloseKey(hKey);
    }
    return (val == 0);
}

// Initialisation des ressources graphiques du thème sombre (teintes feutrées et confortables)
static void InitThemeBrushes(void) {
    if (!g_hDarkWndBrush)         g_hDarkWndBrush         = CreateSolidBrush(RGB(32, 33, 38));
    if (!g_hDarkListBrush)        g_hDarkListBrush        = CreateSolidBrush(RGB(36, 38, 44));
    if (!g_hDarkHeaderBrush)      g_hDarkHeaderBrush      = CreateSolidBrush(RGB(46, 48, 56));
    if (!g_hDarkHeaderHotBrush)   g_hDarkHeaderHotBrush   = CreateSolidBrush(RGB(56, 59, 70));
    if (!g_hDarkHeaderPressBrush) g_hDarkHeaderPressBrush = CreateSolidBrush(RGB(66, 70, 84));
    if (!g_hDarkEditBrush)        g_hDarkEditBrush        = CreateSolidBrush(RGB(42, 44, 52));
    if (!g_hDarkBorderPen)        g_hDarkBorderPen        = CreatePen(PS_SOLID, 1, RGB(62, 65, 76));
}

// Libération propre des ressources graphiques
static void FreeThemeBrushes(void) {
    if (g_hDarkWndBrush)         { DeleteObject(g_hDarkWndBrush); g_hDarkWndBrush = NULL; }
    if (g_hDarkListBrush)        { DeleteObject(g_hDarkListBrush); g_hDarkListBrush = NULL; }
    if (g_hDarkHeaderBrush)      { DeleteObject(g_hDarkHeaderBrush); g_hDarkHeaderBrush = NULL; }
    if (g_hDarkHeaderHotBrush)   { DeleteObject(g_hDarkHeaderHotBrush); g_hDarkHeaderHotBrush = NULL; }
    if (g_hDarkHeaderPressBrush) { DeleteObject(g_hDarkHeaderPressBrush); g_hDarkHeaderPressBrush = NULL; }
    if (g_hDarkEditBrush)        { DeleteObject(g_hDarkEditBrush); g_hDarkEditBrush = NULL; }
    if (g_hDarkBorderPen)        { DeleteObject(g_hDarkBorderPen); g_hDarkBorderPen = NULL; }
}

// Initialisation des points d'entrée Dark Mode dans uxtheme.dll
static void InitDarkModeAPIs(void) {
    static BOOL s_bInitialized = FALSE;
    if (s_bInitialized) return;
    s_bInitialized = TRUE;

    HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme) {
        g_pfnAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
        g_pfnSetPreferredAppMode = (pfnSetPreferredAppMode)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
        g_pfnAllowDarkModeForApp = (pfnAllowDarkModeForApp)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
        g_pfnRefreshImmersiveColorPolicyState = (pfnRefreshImmersiveColorPolicyState)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104));
        g_pfnFlushMenuThemes = (pfnFlushMenuThemes)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));

        if (g_pfnSetPreferredAppMode) {
            g_pfnSetPreferredAppMode(PAM_ALLOW_DARK);
        } else if (g_pfnAllowDarkModeForApp) {
            g_pfnAllowDarkModeForApp(TRUE);
        }

        if (g_pfnRefreshImmersiveColorPolicyState) {
            g_pfnRefreshImmersiveColorPolicyState();
        }
        if (g_pfnFlushMenuThemes) {
            g_pfnFlushMenuThemes();
        }
    }
}

// Application des attributs DWM modernes (Barre de titre sombre immersive sous Windows 10/11)
static void ApplyDwmModernAttributes(HWND hWnd) {
    if (!hWnd) return;
    HMODULE hDwmapi = LoadLibraryW(L"dwmapi.dll");
    if (hDwmapi) {
        typedef HRESULT (WINAPI *pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfnDwmSetWindowAttribute pDwmSet = (pfnDwmSetWindowAttribute)(void*)GetProcAddress(hDwmapi, "DwmSetWindowAttribute");
        if (pDwmSet) {
            BOOL bDarkMode = g_bDarkModeActive;
            // DWMWA_USE_IMMERSIVE_DARK_MODE (20 pour Windows 11 et Win 10 2004+, 19 pour Win 10 1809)
            pDwmSet(hWnd, 20, &bDarkMode, sizeof(bDarkMode));
            pDwmSet(hWnd, 19, &bDarkMode, sizeof(bDarkMode));
        }
        FreeLibrary(hDwmapi);
    }
}

// Subclass des boutons pour un rendu sombre mat, feutré et non éblouissant
static LRESULT CALLBACK ButtonSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (!g_bDarkModeActive) {
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_MOUSELEAVE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // État de survol et de clic
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        BOOL bHover = PtInRect(&rc, pt);
        LRESULT state = SendMessageW(hWnd, BM_GETSTATE, 0, 0);
        BOOL bPressed = (state & BST_PUSHED) != 0;

        // Boutons d'action standards : gris ardoise mat, contours adoucis, texte non éblouissant
        COLORREF clrBg, clrBorder, clrText;
        if (bPressed) {
            clrBg = RGB(36, 38, 44);
            clrBorder = RGB(72, 76, 88);
            clrText = RGB(230, 233, 240);
        } else if (bHover) {
            clrBg = RGB(52, 55, 64);
            clrBorder = RGB(82, 87, 100);
            clrText = RGB(235, 238, 245);
        } else {
            clrBg = RGB(43, 45, 52);
            clrBorder = RGB(64, 67, 78);
            clrText = RGB(210, 214, 222);
        }

        // Fond et bordure
        HBRUSH hBrBg = CreateSolidBrush(clrBg);
        HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrBorder);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hBrBg);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenBorder);

        RoundRect(hMemDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        SelectObject(hMemDC, hOldBr);
        SelectObject(hMemDC, hOldPen);
        DeleteObject(hBrBg);
        DeleteObject(hPenBorder);

        // Texte du bouton
        WCHAR szText[128] = { 0 };
        GetWindowTextW(hWnd, szText, sizeof(szText) / sizeof(WCHAR));

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, clrText);
        HFONT hOldFont = NULL;
        if (g_hFont) hOldFont = (HFONT)SelectObject(hMemDC, g_hFont);

        DrawTextW(hMemDC, szText, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass de la barre d'état (StatusBar) : fond feutré, sans bordures blanches 3D et texte atténué
static LRESULT CALLBACK StatusBarSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (!g_bDarkModeActive) {
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_ERASEBKGND) {
        return 1;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // Fond sombre feutré et non éblouissant
        HBRUSH hBrBg = CreateSolidBrush(RGB(28, 29, 34));
        FillRect(hMemDC, &rcClient, hBrBg);
        DeleteObject(hBrBg);

        // Trait supérieur discret de démarcation
        HPEN hSepPen = CreatePen(PS_SOLID, 1, RGB(46, 48, 56));
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hSepPen);
        MoveToEx(hMemDC, 0, 0, NULL);
        LineTo(hMemDC, rcClient.right, 0);

        HFONT hOldFont = NULL;
        if (g_hFont) hOldFont = (HFONT)SelectObject(hMemDC, g_hFont);
        SetBkMode(hMemDC, TRANSPARENT);
        // Couleur de texte feutrée (gris doux anti-éblouissement)
        SetTextColor(hMemDC, RGB(168, 172, 182));

        int nParts = (int)SendMessageW(hWnd, SB_GETPARTS, 0, 0);
        for (int i = 0; i < nParts; i++) {
            RECT rcPart;
            SendMessageW(hWnd, SB_GETRECT, i, (LPARAM)&rcPart);

            // Séparateurs verticaux subtils entre sections
            if (i > 0) {
                MoveToEx(hMemDC, rcPart.left, rcPart.top + 3, NULL);
                LineTo(hMemDC, rcPart.left, rcPart.bottom - 3);
            }

            WCHAR szPartText[256] = { 0 };
            SendMessageW(hWnd, SB_GETTEXTW, i, (LPARAM)szPartText);

            if (szPartText[0] != L'\0') {
                RECT rcText = rcPart;
                rcText.left += 8;
                rcText.right -= 8;
                DrawTextW(hMemDC, szPartText, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }

        SelectObject(hMemDC, hOldPen);
        DeleteObject(hSepPen);
        if (hOldFont) SelectObject(hMemDC, hOldFont);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);
        EndPaint(hWnd, &ps);
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Dessin d'une carte de métrique (CPU, RAM, GPU) avec graphique d'historique 1h (3600s) et infobulle au survol
static void DrawPerfCard(
    HDC hdc, RECT rcCard, int cardIdx, const WCHAR *szTitle, const WCHAR *szSub,
    const double *history, const TopProcSample *topHistory, int count, double curVal,
    COLORREF clrAccent, COLORREF clrPeak, COLORREF clrArea,
    BOOL bIsDark, int hoverCard, int hoverX, int hoverY
) {
    // 1. Fond et bordure douce de la carte
    COLORREF clrCardBg = bIsDark ? RGB(30, 32, 40) : RGB(255, 255, 255);
    COLORREF clrCardBorder = bIsDark ? RGB(48, 52, 64) : RGB(226, 232, 240);
    HBRUSH hBrBg = CreateSolidBrush(clrCardBg);
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrCardBorder);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBrBg);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenBorder);
    RoundRect(hdc, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrBg);
    DeleteObject(hPenBorder);

    // 2. En-tête : Pastille de ressource, Titre et Valeur actuelle
    SetBkMode(hdc, TRANSPARENT);

    // Pastille colorée
    RECT rcDot = { rcCard.left + 10, rcCard.top + 9, rcCard.left + 14, rcCard.top + 21 };
    HBRUSH hBrDot = CreateSolidBrush(clrAccent);
    FillRect(hdc, &rcDot, hBrDot);
    DeleteObject(hBrDot);

    // Titre de la ressource
    RECT rcTitle = { rcCard.left + 20, rcCard.top + 6, rcCard.left + 140, rcCard.top + 22 };
    SetTextColor(hdc, bIsDark ? RGB(232, 236, 244) : RGB(30, 41, 59));
    HFONT hOldFont = NULL;
    if (g_hFontBold) hOldFont = (HFONT)SelectObject(hdc, g_hFontBold);
    else if (g_hFont) hOldFont = (HFONT)SelectObject(hdc, g_hFont);
    DrawTextW(hdc, szTitle, -1, &rcTitle, DT_SINGLELINE | DT_VCENTER);

    // Valeur actuelle en gros à droite
    WCHAR szValStr[64];
    swprintf_s(szValStr, sizeof(szValStr) / sizeof(WCHAR), L"%.1f %%", curVal);
    RECT rcVal = { rcCard.right - 120, rcCard.top + 6, rcCard.right - 10, rcCard.top + 22 };
    SetTextColor(hdc, clrPeak);
    DrawTextW(hdc, szValStr, -1, &rcVal, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);

    // Sous-titre descriptif
    RECT rcSub = { rcCard.left + 20, rcCard.top + 21, rcCard.right - 10, rcCard.top + 33 };
    SetTextColor(hdc, bIsDark ? RGB(148, 154, 168) : RGB(100, 116, 139));
    if (g_hFontSmall) SelectObject(hdc, g_hFontSmall);
    DrawTextW(hdc, szSub, -1, &rcSub, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    if (hOldFont) SelectObject(hdc, hOldFont);

    // 3. Cadre intérieur du graphique (Historique 1h)
    RECT rcGraph = { rcCard.left + 8, rcCard.top + 34, rcCard.right - 8, rcCard.bottom - 7 };
    int gW = rcGraph.right - rcGraph.left;
    int gH = rcGraph.bottom - rcGraph.top;
    if (gW < 20 || gH < 15) return;

    // Fond du graphique
    COLORREF clrGraphBg = bIsDark ? RGB(20, 22, 28) : RGB(248, 250, 252);
    HBRUSH hBrGraph = CreateSolidBrush(clrGraphBg);
    HPEN hPenGraphBorder = CreatePen(PS_SOLID, 1, bIsDark ? RGB(38, 42, 52) : RGB(226, 232, 240));
    hOldBr = (HBRUSH)SelectObject(hdc, hBrGraph);
    hOldPen = (HPEN)SelectObject(hdc, hPenGraphBorder);
    Rectangle(hdc, rcGraph.left, rcGraph.top, rcGraph.right, rcGraph.bottom);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrGraph);
    DeleteObject(hPenGraphBorder);

    // Lignes de grille horizontales (50%)
    HPEN hGridPen = CreatePen(PS_SOLID, 1, bIsDark ? RGB(32, 35, 45) : RGB(235, 238, 243));
    hOldPen = (HPEN)SelectObject(hdc, hGridPen);
    int y50 = rcGraph.top + gH / 2;
    MoveToEx(hdc, rcGraph.left + 1, y50, NULL);
    LineTo(hdc, rcGraph.right - 1, y50);

    // Lignes verticales de grille (repères 45m, 30m, 15m)
    for (int s = 1; s <= 3; s++) {
        int xGrid = rcGraph.left + (gW * s) / 4;
        MoveToEx(hdc, xGrid, rcGraph.top + 1, NULL);
        LineTo(hdc, xGrid, rcGraph.bottom - 1);
    }
    SelectObject(hdc, hOldPen);
    DeleteObject(hGridPen);

    // Repères temporels au bas du graphique (60m, 45m, 30m, 15m, 0s)
    if (gW >= 120) {
        if (g_hFontSmall) SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, bIsDark ? RGB(90, 95, 110) : RGB(160, 168, 180));
        RECT rcT60 = { rcGraph.left + 3, rcGraph.bottom - 12, rcGraph.left + 30, rcGraph.bottom - 1 };
        DrawTextW(hdc, L"60m", -1, &rcT60, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        RECT rcT45 = { rcGraph.left + gW / 4 - 10, rcGraph.bottom - 12, rcGraph.left + gW / 4 + 14, rcGraph.bottom - 1 };
        DrawTextW(hdc, L"45m", -1, &rcT45, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        RECT rcT30 = { rcGraph.left + gW / 2 - 10, rcGraph.bottom - 12, rcGraph.left + gW / 2 + 14, rcGraph.bottom - 1 };
        DrawTextW(hdc, L"30m", -1, &rcT30, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        RECT rcT15 = { rcGraph.left + (gW * 3) / 4 - 10, rcGraph.bottom - 12, rcGraph.left + (gW * 3) / 4 + 14, rcGraph.bottom - 1 };
        DrawTextW(hdc, L"15m", -1, &rcT15, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        RECT rcT0 = { rcGraph.right - 22, rcGraph.bottom - 12, rcGraph.right - 3, rcGraph.bottom - 1 };
        DrawTextW(hdc, L"0s", -1, &rcT0, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
    }

    // 4. Tracé fluide de la courbe et de la zone ombrée (Échantillonnage optimisé par pixel sur 3600 points)
    if (count > 1 && gW > 4) {
        int numPoints = gW - 2;
        POINT *pts = (POINT*)malloc(sizeof(POINT) * (numPoints + 2));
        if (pts) {
            for (int p = 0; p < numPoints; p++) {
                int idx = (int)(((double)p / (double)(numPoints - 1)) * (count - 1));
                if (idx < 0) idx = 0;
                if (idx >= count) idx = count - 1;

                double pct = history[idx];
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;

                int px = rcGraph.left + 1 + p;
                int py = rcGraph.bottom - 1 - (int)((pct / 100.0) * (gH - 3));
                if (py < rcGraph.top + 1) py = rcGraph.top + 1;
                if (py > rcGraph.bottom - 1) py = rcGraph.bottom - 1;

                pts[p].x = px;
                pts[p].y = py;
            }

            // Polygone fermé pour le remplissage sous la courbe
            pts[numPoints].x = pts[numPoints - 1].x;
            pts[numPoints].y = rcGraph.bottom - 1;
            pts[numPoints + 1].x = pts[0].x;
            pts[numPoints + 1].y = rcGraph.bottom - 1;

            HBRUSH hBrArea = CreateSolidBrush(clrArea);
            HPEN hPenNull = CreatePen(PS_NULL, 0, 0);
            hOldBr = (HBRUSH)SelectObject(hdc, hBrArea);
            hOldPen = (HPEN)SelectObject(hdc, hPenNull);
            Polygon(hdc, pts, numPoints + 2);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hBrArea);
            DeleteObject(hPenNull);

            // Courbe principale
            HPEN hPenLine = CreatePen(PS_SOLID, 2, clrAccent);
            hOldPen = (HPEN)SelectObject(hdc, hPenLine);
            Polyline(hdc, pts, numPoints);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPenLine);

            // Point lumineux final sur l'échantillon actuel (à l'extrémité droite)
            int lastX = pts[numPoints - 1].x;
            int lastY = pts[numPoints - 1].y;
            HBRUSH hBrDotEnd = CreateSolidBrush(clrPeak);
            HPEN hPenDotBorder = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            hOldBr = (HBRUSH)SelectObject(hdc, hBrDotEnd);
            hOldPen = (HPEN)SelectObject(hdc, hPenDotBorder);
            Ellipse(hdc, lastX - 3, lastY - 3, lastX + 3, lastY + 3);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hBrDotEnd);
            DeleteObject(hPenDotBorder);

            free(pts);
        }
    }

    // 5. Survol interactif de la souris (Affiche le tracker et le processus responsable à cet instant T)
    if (hoverCard == cardIdx && hoverX >= rcGraph.left + 1 && hoverX <= rcGraph.right - 1 && gW > 4) {
        int relX = hoverX - (rcGraph.left + 1);
        int sampleIdx = (int)(((double)relX / (double)(gW - 2)) * (count - 1));
        if (sampleIdx < 0) sampleIdx = 0;
        if (sampleIdx >= count) sampleIdx = count - 1;

        double sampleVal = history[sampleIdx];
        if (sampleVal < 0.0) sampleVal = 0.0;
        if (sampleVal > 100.0) sampleVal = 100.0;

        int sampleY = rcGraph.bottom - 1 - (int)((sampleVal / 100.0) * (gH - 3));
        if (sampleY < rcGraph.top + 1) sampleY = rcGraph.top + 1;
        if (sampleY > rcGraph.bottom - 1) sampleY = rcGraph.bottom - 1;

        // Ligne verticale de repère temporel sous le curseur
        HPEN hPenTracker = CreatePen(PS_DOT, 1, bIsDark ? RGB(200, 210, 230) : RGB(70, 80, 95));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenTracker);
        MoveToEx(hdc, hoverX, rcGraph.top + 1, NULL);
        LineTo(hdc, hoverX, rcGraph.bottom - 1);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPenTracker);

        // Point cible lumineux sur la courbe
        HBRUSH hBrTarget = CreateSolidBrush(clrPeak);
        HPEN hPenTarget = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBrTarget);
        hOldPen = (HPEN)SelectObject(hdc, hPenTarget);
        Ellipse(hdc, hoverX - 4, sampleY - 4, hoverX + 5, sampleY + 5);
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrTarget);
        DeleteObject(hPenTarget);

        // Textes de l'infobulle (temps écoulé, ressource, et top processus consommateur)
        int secondsAgo = (count - 1) - sampleIdx;
        int minAgo = secondsAgo / 60;
        int secAgo = secondsAgo % 60;

        WCHAR szTimeLine[64];
        if (minAgo > 0) {
            swprintf_s(szTimeLine, sizeof(szTimeLine) / sizeof(WCHAR), GetResString(IDS_TIP_TIME_MIN_SEC), minAgo, secAgo, sampleVal);
        } else {
            swprintf_s(szTimeLine, sizeof(szTimeLine) / sizeof(WCHAR), GetResString(IDS_TIP_TIME_SEC), secAgo, sampleVal);
        }

        WCHAR szProcLine[128];
        WCHAR szUsageLine[64];
        const TopProcSample *tp = (topHistory && sampleIdx < count) ? &topHistory[sampleIdx] : NULL;

        if (tp && tp->szName[0] != L'\0') {
            swprintf_s(szProcLine, sizeof(szProcLine) / sizeof(WCHAR), GetResString(IDS_TIP_TOP_PROC), tp->szName, tp->pid);
            if (cardIdx == 1) { // RAM
                swprintf_s(szUsageLine, sizeof(szUsageLine) / sizeof(WCHAR), GetResString(IDS_TIP_USAGE_RAM), (UINT)tp->usage);
            } else {
                swprintf_s(szUsageLine, sizeof(szUsageLine) / sizeof(WCHAR), GetResString(IDS_TIP_USAGE_PCT), tp->usage);
            }
        } else {
            wcsncpy_s(szProcLine, 128, GetResString(IDS_TIP_NO_DATA), _TRUNCATE);
            szUsageLine[0] = L'\0';
        }

        // Boîte d'infobulle flottante
        int tipW = 175;
        int tipH = 46;
        int tipX = hoverX + 10;
        int tipY = hoverY - tipH / 2;

        if (tipX + tipW > rcCard.right - 6) {
            tipX = hoverX - tipW - 10;
        }
        if (tipX < rcCard.left + 6) {
            tipX = rcCard.left + 6;
        }
        if (tipY < rcCard.top + 4) {
            tipY = rcCard.top + 4;
        }
        if (tipY + tipH > rcCard.bottom - 4) {
            tipY = rcCard.bottom - tipH - 4;
        }

        RECT rcTip = { tipX, tipY, tipX + tipW, tipY + tipH };

        COLORREF clrTipBg = bIsDark ? RGB(16, 18, 24) : RGB(255, 255, 255);
        HBRUSH hBrTip = CreateSolidBrush(clrTipBg);
        HPEN hPenTip = CreatePen(PS_SOLID, 1, clrAccent);
        hOldBr = (HBRUSH)SelectObject(hdc, hBrTip);
        hOldPen = (HPEN)SelectObject(hdc, hPenTip);
        RoundRect(hdc, rcTip.left, rcTip.top, rcTip.right, rcTip.bottom, 6, 6);
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrTip);
        DeleteObject(hPenTip);

        HFONT hOldF = NULL;
        if (g_hFontSmall) hOldF = (HFONT)SelectObject(hdc, g_hFontSmall);

        RECT rcL1 = { rcTip.left + 8, rcTip.top + 3, rcTip.right - 8, rcTip.top + 16 };
        SetTextColor(hdc, clrPeak);
        DrawTextW(hdc, szTimeLine, -1, &rcL1, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        RECT rcL2 = { rcTip.left + 8, rcTip.top + 16, rcTip.right - 8, rcTip.top + 29 };
        SetTextColor(hdc, bIsDark ? RGB(232, 236, 245) : RGB(25, 30, 42));
        DrawTextW(hdc, szProcLine, -1, &rcL2, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        RECT rcL3 = { rcTip.left + 8, rcTip.top + 29, rcTip.right - 8, rcTip.top + 42 };
        SetTextColor(hdc, bIsDark ? RGB(160, 168, 182) : RGB(95, 105, 120));
        DrawTextW(hdc, szUsageLine, -1, &rcL3, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        if (hOldF) SelectObject(hdc, hOldF);
    }
}

// Subclass pour le bandeau graphique des performances (CPU, RAM, GPU) au-dessus de la liste des processus
static LRESULT CALLBACK GraphBannerSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    static int s_nHoverCard = -1;
    static int s_nHoverX = -1;
    static int s_nHoverY = -1;
    static BOOL s_bTrackingMouse = FALSE;

    if (uMsg == WM_ERASEBKGND) {
        return 1;
    }

    if (uMsg == WM_MOUSEMOVE) {
        if (!s_bTrackingMouse) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            s_bTrackingMouse = TRUE;
        }

        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right - rcClient.left;
        int gap = 8;
        int cardW = (w - (gap * 2)) / 3;
        if (cardW < 50) cardW = 50;

        int newCard = -1;
        if (mx >= 0 && mx < cardW) newCard = 0;
        else if (mx >= cardW + gap && mx < cardW * 2 + gap) newCard = 1;
        else if (mx >= (cardW + gap) * 2 && mx < w) newCard = 2;

        if (newCard != s_nHoverCard || mx != s_nHoverX || my != s_nHoverY) {
            s_nHoverCard = newCard;
            s_nHoverX = mx;
            s_nHoverY = my;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    if (uMsg == WM_MOUSELEAVE) {
        s_bTrackingMouse = FALSE;
        s_nHoverCard = -1;
        s_nHoverX = -1;
        s_nHoverY = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        int w = rcClient.right - rcClient.left;
        int h = rcClient.bottom - rcClient.top;

        if (w <= 0 || h <= 0) {
            EndPaint(hWnd, &ps);
            return 0;
        }

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // Fond du conteneur en accord avec la fenêtre principale
        HBRUSH hBrBg = g_bDarkModeActive ? g_hDarkWndBrush : (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(hMemDC, &rcClient, hBrBg);

        int gap = 8;
        int cardW = (w - (gap * 2)) / 3;
        if (cardW < 50) cardW = 50;

        // 1. Carte CPU (Vert émeraude)
        RECT rcCpu = { 0, 0, cardW, h };
        DrawPerfCard(
            hMemDC, rcCpu, 0, GetResString(IDS_GRAPH_CPU_TITLE), GetResString(IDS_GRAPH_CPU_SUB),
            g_PerfCpuHistory, g_TopCpuHistory, PERF_HISTORY_POINTS, g_LastCpuPct,
            RGB(16, 185, 129), RGB(52, 211, 153),
            g_bDarkModeActive ? RGB(10, 48, 38) : RGB(220, 252, 231),
            g_bDarkModeActive, s_nHoverCard, s_nHoverX, s_nHoverY
        );

        // 2. Carte Mémoire RAM (Violet / Pourpre)
        RECT rcRam = { cardW + gap, 0, cardW * 2 + gap, h };
        WCHAR szRamSub[64];
        if (g_LastTotalRamGb > 0.0) {
            swprintf_s(szRamSub, sizeof(szRamSub) / sizeof(WCHAR), GetResString(IDS_GRAPH_RAM_SUB_FMT), g_LastUsedRamGb, g_LastTotalRamGb);
        } else {
            wcsncpy_s(szRamSub, 64, GetResString(IDS_GRAPH_RAM_SUB_DEFAULT), _TRUNCATE);
        }
        DrawPerfCard(
            hMemDC, rcRam, 1, GetResString(IDS_GRAPH_RAM_TITLE), szRamSub,
            g_PerfRamHistory, g_TopRamHistory, PERF_HISTORY_POINTS, g_LastMemPct,
            RGB(168, 85, 247), RGB(192, 132, 252),
            g_bDarkModeActive ? RGB(44, 24, 66) : RGB(243, 232, 255),
            g_bDarkModeActive, s_nHoverCard, s_nHoverX, s_nHoverY
        );

        // 3. Carte GPU (Cyan)
        RECT rcGpu = { (cardW + gap) * 2, 0, w, h };
        DrawPerfCard(
            hMemDC, rcGpu, 2, GetResString(IDS_GRAPH_GPU_TITLE), GetResString(IDS_GRAPH_GPU_SUB),
            g_PerfGpuHistory, g_TopGpuHistory, PERF_HISTORY_POINTS, g_LastGpuPct,
            RGB(6, 182, 212), RGB(34, 211, 238),
            g_bDarkModeActive ? RGB(8, 44, 56) : RGB(207, 250, 254),
            g_bDarkModeActive, s_nHoverCard, s_nHoverX, s_nHoverY
        );

        BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);
        EndPaint(hWnd, &ps);
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass de l'en-tête (SysHeader32) pour éviter le scintillement d'effacement
static LRESULT CALLBACK HeaderSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (uMsg == WM_ERASEBKGND && g_bDarkModeActive) {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hDarkHeaderBrush);
        return 1;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass de la ListView (SysListView32) pour le rendu personnalisé moderne des colonnes (en-tête) en mode sombre
static LRESULT CALLBACK ListViewSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (uMsg == WM_NOTIFY) {
        LPNMHDR lpnm = (LPNMHDR)lParam;
        HWND hHeader = ListView_GetHeader(hWnd);
        if (hHeader && lpnm->hwndFrom == hHeader && lpnm->code == NM_CUSTOMDRAW) {
            if (g_bDarkModeActive) {
                LPNMCUSTOMDRAW pnmcd = (LPNMCUSTOMDRAW)lParam;
                if (pnmcd->dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                }
                if (pnmcd->dwDrawStage == CDDS_ITEMPREPAINT) {
                    RECT rc = pnmcd->rc;
                    int iCol = (int)pnmcd->dwItemSpec;

                    // Choix de la couleur de fond selon l'état (hover, pressé, normal)
                    HBRUSH hBr = g_hDarkHeaderBrush;
                    if (pnmcd->uItemState & CDIS_SELECTED) {
                        hBr = g_hDarkHeaderPressBrush;
                    } else if (pnmcd->uItemState & CDIS_HOT) {
                        hBr = g_hDarkHeaderHotBrush;
                    }
                    FillRect(pnmcd->hdc, &rc, hBr);

                    // Trait vertical de séparation entre colonnes
                    HPEN hOldPen = (HPEN)SelectObject(pnmcd->hdc, g_hDarkBorderPen);
                    MoveToEx(pnmcd->hdc, rc.right - 1, rc.top + 3, NULL);
                    LineTo(pnmcd->hdc, rc.right - 1, rc.bottom - 3);

                    // Trait inférieur de séparation avec la liste
                    MoveToEx(pnmcd->hdc, rc.left, rc.bottom - 1, NULL);
                    LineTo(pnmcd->hdc, rc.right, rc.bottom - 1);
                    SelectObject(pnmcd->hdc, hOldPen);

                    // Récupération du titre de la colonne (avec flèche ▲ / ▼ de tri)
                    WCHAR szText[128] = { 0 };
                    HDITEMW hdi = { 0 };
                    hdi.mask = HDI_TEXT | HDI_FORMAT;
                    hdi.pszText = szText;
                    hdi.cchTextMax = sizeof(szText) / sizeof(WCHAR);
                    Header_GetItem(hHeader, iCol, &hdi);

                    if (szText[0] == L'\0' && iCol >= 0 && iCol < COL_COUNT) {
                        if (iCol == g_SortColumn) {
                            wsprintfW(szText, L"%s %s", GetColumnTitle(iCol), g_bSortAscending ? L"▲" : L"▼");
                        } else {
                            wcsncpy_s(szText, 128, GetColumnTitle(iCol), _TRUNCATE);
                        }
                    }

                    // Alignement selon la définition de la colonne (droite pour valeurs numériques, gauche pour texte)
                    DWORD dtFlags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
                    if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT) {
                        dtFlags |= DT_RIGHT;
                    } else {
                        dtFlags |= DT_LEFT;
                    }

                    // Marges intérieures horizontales
                    rc.left += 8;
                    rc.right -= 8;

                    HFONT hOldFont = NULL;
                    if (g_hFont) {
                        hOldFont = (HFONT)SelectObject(pnmcd->hdc, g_hFont);
                    }

                    // Rendu du texte des en-têtes en teinte douce et lisible
                    SetTextColor(pnmcd->hdc, RGB(228, 230, 236));
                    SetBkMode(pnmcd->hdc, TRANSPARENT);
                    DrawTextW(pnmcd->hdc, szText, -1, &rc, dtFlags);

                    if (hOldFont) {
                        SelectObject(pnmcd->hdc, hOldFont);
                    }

                    return CDRF_SKIPDEFAULT;
                }
                if (pnmcd->dwDrawStage == CDDS_POSTPAINT) {
                    // Remplissage de l'espace restant à droite de la dernière colonne
                    int count = Header_GetItemCount(hHeader);
                    if (count > 0) {
                        RECT rcLast;
                        if (Header_GetItemRect(hHeader, count - 1, &rcLast)) {
                            RECT rcClient;
                            GetClientRect(hHeader, &rcClient);
                            if (rcLast.right < rcClient.right) {
                                RECT rcFill = { rcLast.right, 0, rcClient.right, rcClient.bottom };
                                FillRect(pnmcd->hdc, &rcFill, g_hDarkHeaderBrush);

                                HPEN hOldPen = (HPEN)SelectObject(pnmcd->hdc, g_hDarkBorderPen);
                                MoveToEx(pnmcd->hdc, rcFill.left, rcFill.bottom - 1, NULL);
                                LineTo(pnmcd->hdc, rcFill.right, rcFill.bottom - 1);
                                SelectObject(pnmcd->hdc, hOldPen);
                            }
                        }
                    }
                    return CDRF_DODEFAULT;
                }
            }
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Application du thème moderne officiel Windows (Mode sombre dynamique et Explorer moderne)
static void ApplyModernWindowsTheme(HWND hWnd, HWND hListView) {
    g_bDarkModeActive = IsWindowsDarkModeActive();
    InitThemeBrushes();
    InitDarkModeAPIs();

    if (g_pfnAllowDarkModeForWindow) {
        if (hWnd) g_pfnAllowDarkModeForWindow(hWnd, g_bDarkModeActive);
        if (hListView) {
            g_pfnAllowDarkModeForWindow(hListView, g_bDarkModeActive);
            HWND hHeader = ListView_GetHeader(hListView);
            if (hHeader) g_pfnAllowDarkModeForWindow(hHeader, g_bDarkModeActive);
        }
        if (g_hStatusBar) g_pfnAllowDarkModeForWindow(g_hStatusBar, g_bDarkModeActive);
        if (g_hBtnRefresh) g_pfnAllowDarkModeForWindow(g_hBtnRefresh, g_bDarkModeActive);
        if (g_hBtnAutoRefresh) g_pfnAllowDarkModeForWindow(g_hBtnAutoRefresh, g_bDarkModeActive);
        if (g_hBtnHideWindows) g_pfnAllowDarkModeForWindow(g_hBtnHideWindows, g_bDarkModeActive);
        if (g_hBtnExclusions) g_pfnAllowDarkModeForWindow(g_hBtnExclusions, g_bDarkModeActive);
        if (g_hStaticSearch) g_pfnAllowDarkModeForWindow(g_hStaticSearch, g_bDarkModeActive);
        if (g_hEditSearch) g_pfnAllowDarkModeForWindow(g_hEditSearch, g_bDarkModeActive);
        if (g_hBtnSettings) g_pfnAllowDarkModeForWindow(g_hBtnSettings, g_bDarkModeActive);
        if (g_hBtnAbout) g_pfnAllowDarkModeForWindow(g_hBtnAbout, g_bDarkModeActive);
        if (g_hGraphBanner) g_pfnAllowDarkModeForWindow(g_hGraphBanner, g_bDarkModeActive);
    }

    HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme) {
        typedef HRESULT (WINAPI *pfnSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
        pfnSetWindowTheme pSetWindowTheme = (pfnSetWindowTheme)(void*)GetProcAddress(hUxTheme, "SetWindowTheme");
        if (pSetWindowTheme) {
            if (hListView) {
                HWND hHeader = ListView_GetHeader(hListView);
                if (g_bDarkModeActive) {
                    pSetWindowTheme(hListView, L"DarkMode_Explorer", NULL);
                    // Crucial : Ne PAS appliquer DarkMode_ItemsView sur hHeader car UxTheme
                    // peint l'en-tête en noir opaque sur Windows 10/11 sans dessiner le texte en blanc.
                    // Notre custom draw assure un rendu de colonnes moderne, contrasté et lisible.
                    pSetWindowTheme(hHeader, L"", L"");
                    ListView_SetBkColor(hListView, RGB(36, 38, 44));
                    ListView_SetTextBkColor(hListView, RGB(36, 38, 44));
                    ListView_SetTextColor(hListView, RGB(242, 244, 248));
                } else {
                    pSetWindowTheme(hListView, L"Explorer", NULL);
                    pSetWindowTheme(hHeader, L"Explorer", NULL);
                    ListView_SetBkColor(hListView, RGB(255, 255, 255));
                    ListView_SetTextBkColor(hListView, CLR_NONE);
                    ListView_SetTextColor(hListView, RGB(0, 0, 0));
                }
            }

            if (g_hStatusBar) {
                if (g_bDarkModeActive) {
                    pSetWindowTheme(g_hStatusBar, L"DarkMode_Explorer", NULL);
                    SendMessageW(g_hStatusBar, SB_SETBKCOLOR, 0, (LPARAM)RGB(28, 29, 34));
                } else {
                    pSetWindowTheme(g_hStatusBar, L"Explorer", NULL);
                    SendMessageW(g_hStatusBar, SB_SETBKCOLOR, 0, (LPARAM)CLR_DEFAULT);
                }
            }

            if (g_hEditSearch) {
                pSetWindowTheme(g_hEditSearch, g_bDarkModeActive ? L"DarkMode_CFD" : L"Explorer", NULL);
            }
        }
        FreeLibrary(hUxTheme);
    }

    if (hWnd) {
        SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)(g_bDarkModeActive ? g_hDarkWndBrush : (HBRUSH)(COLOR_BTNFACE + 1)));
    }

    ApplyDwmModernAttributes(hWnd);
}

// ============================================================================
// Mise à jour en direct de la langue dans toute l'interface
// ============================================================================
void ApplyLanguageToUI(HWND hWnd) {
    if (!hWnd) return;

    SetWindowTextW(hWnd, GetResString(IDS_APP_TITLE));

    if (g_hBtnAutoRefresh) {
        SetWindowTextW(g_hBtnAutoRefresh, g_bAutoRefresh ? GetResString(IDS_BTN_AUTOREFRESH_ON) : GetResString(IDS_BTN_AUTOREFRESH_OFF));
    }
    if (g_hBtnRefresh) {
        SetWindowTextW(g_hBtnRefresh, GetResString(IDS_BTN_REFRESH));
    }
    if (g_hBtnHideWindows) {
        SetWindowTextW(g_hBtnHideWindows, g_bHideWindowsProcesses ? GetResString(IDS_BTN_WINDOWS_OFF) : GetResString(IDS_BTN_WINDOWS_ON));
    }
    UpdateExclusionsButtonText();
    if (g_hStaticSearch) {
        SetWindowTextW(g_hStaticSearch, GetResString(IDS_LABEL_FILTER));
    }
    if (g_hBtnSettings) {
        SetWindowTextW(g_hBtnSettings, GetResString(IDS_BTN_SETTINGS));
    }
    if (g_hBtnAbout) {
        SetWindowTextW(g_hBtnAbout, GetResString(IDS_BTN_ABOUT));
    }

    UpdateHeaderSortArrow();
    if (g_hGraphBanner && IsWindow(g_hGraphBanner)) {
        InvalidateRect(g_hGraphBanner, NULL, FALSE);
    }
    RefreshProcessList();
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// Utilitaire de positionnement des boutons alignés à droite dans la barre d'outils
static void LayoutToolbarRightButtons(int clientWidth) {
    int rightMargin = 10;
    int aboutW = 105;
    int settingsW = 120;
    int gap = 6;

    int aboutX = clientWidth - rightMargin - aboutW;
    int settingsX = aboutX - gap - settingsW;
    if (settingsX < 825) {
        settingsX = 825;
        aboutX = settingsX + settingsW + gap;
    }

    if (g_hBtnSettings) {
        MoveWindow(g_hBtnSettings, settingsX, 10, settingsW, 28, TRUE);
    }
    if (g_hBtnAbout) {
        MoveWindow(g_hBtnAbout, aboutX, 10, aboutW, 28, TRUE);
    }
}

// ============================================================================
// Fenêtre modale Paramètres (Langue, Auto-refresh, Processus Windows)
// ============================================================================
typedef struct {
    HWND hParent;
    HWND hComboLang;
    HWND hLblActiveLang;
    HWND hChkAuto;
    HWND hChkHideWin;
    HWND hBtnOk;
    HWND hBtnCancel;
    BOOL bDone;
} SettingsDlgState;

static void UpdateActiveLangPreviewLabel(SettingsDlgState *pState) {
    if (!pState || !pState->hComboLang || !pState->hLblActiveLang) return;
    int sel = (int)SendMessageW(pState->hComboLang, CB_GETCURSEL, 0, 0);
    int resolved = APP_LANG_EN;
    if (sel >= APP_LANG_EN && sel <= APP_LANG_PT) {
        resolved = sel;
    } else {
        LANGID langId = GetUserDefaultUILanguage();
        switch (PRIMARYLANGID(langId)) {
        case LANG_FRENCH:     resolved = APP_LANG_FR; break;
        case LANG_SPANISH:    resolved = APP_LANG_ES; break;
        case LANG_GERMAN:     resolved = APP_LANG_DE; break;
        case LANG_ITALIAN:    resolved = APP_LANG_IT; break;
        case LANG_PORTUGUESE: resolved = APP_LANG_PT; break;
        default:              resolved = APP_LANG_EN; break;
        }
    }

    UINT langNameId = IDS_SETTINGS_LANG_EN;
    switch (resolved) {
    case APP_LANG_FR: langNameId = IDS_SETTINGS_LANG_FR; break;
    case APP_LANG_ES: langNameId = IDS_SETTINGS_LANG_ES; break;
    case APP_LANG_DE: langNameId = IDS_SETTINGS_LANG_DE; break;
    case APP_LANG_IT: langNameId = IDS_SETTINGS_LANG_IT; break;
    case APP_LANG_PT: langNameId = IDS_SETTINGS_LANG_PT; break;
    default:          langNameId = IDS_SETTINGS_LANG_EN; break;
    }
    WCHAR szBuf[128];
    swprintf_s(szBuf, sizeof(szBuf) / sizeof(WCHAR), GetResString(IDS_SETTINGS_ACTIVE_LANG_FMT), GetResString(langNameId));
    SetWindowTextW(pState->hLblActiveLang, szBuf);
}

static LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsDlgState *pState = (SettingsDlgState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (message) {
    case WM_CREATE: {
        CREATESTRUCTW *pCs = (CREATESTRUCTW*)lParam;
        pState = (SettingsDlgState*)pCs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pState);

        HINSTANCE hInst = GetModuleHandleW(NULL);
        HFONT hFont = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        ApplyDwmModernAttributes(hDlg);
        if (g_pfnAllowDarkModeForWindow) {
            g_pfnAllowDarkModeForWindow(hDlg, g_bDarkModeActive);
        }

        // Label Langue
        HWND hLblLang = CreateWindowExW(
            0, L"STATIC", GetResString(IDS_SETTINGS_LANG_LABEL),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, 360, 20,
            hDlg, NULL, hInst, NULL
        );
        SendMessageW(hLblLang, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Liste déroulante Langue
        pState->hComboLang = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 44, 360, 200,
            hDlg, (HMENU)IDC_SET_COMBO_LANG, hInst, NULL
        );
        SendMessageW(pState->hComboLang, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_AUTO));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_EN));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_FR));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_ES));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_DE));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_IT));
        SendMessageW(pState->hComboLang, CB_ADDSTRING, 0, (LPARAM)GetResString(IDS_SETTINGS_LANG_PT));
        SendMessageW(pState->hComboLang, CB_SETCURSEL, (WPARAM)g_nLanguageSetting, 0);

        // Indication de la langue active résolue
        pState->hLblActiveLang = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 74, 360, 18,
            hDlg, (HMENU)IDC_SET_ACTIVE_LANG, hInst, NULL
        );
        SendMessageW(pState->hLblActiveLang, WM_SETFONT, (WPARAM)(g_hFontSmall ? g_hFontSmall : hFont), TRUE);
        UpdateActiveLangPreviewLabel(pState);

        // Case à cocher Auto-refresh
        pState->hChkAuto = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_SETTINGS_AUTOREFRESH),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            20, 108, 360, 24,
            hDlg, (HMENU)IDC_SET_CHK_AUTO, hInst, NULL
        );
        SendMessageW(pState->hChkAuto, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(pState->hChkAuto, BM_SETCHECK, g_bAutoRefresh ? BST_CHECKED : BST_UNCHECKED, 0);

        // Case à cocher Masquer les processus Windows
        pState->hChkHideWin = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_SETTINGS_HIDE_WINDOWS),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            20, 138, 360, 24,
            hDlg, (HMENU)IDC_SET_CHK_HIDEWIN, hInst, NULL
        );
        SendMessageW(pState->hChkHideWin, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(pState->hChkHideWin, BM_SETCHECK, g_bHideWindowsProcesses ? BST_CHECKED : BST_UNCHECKED, 0);

        // Boutons OK / Annuler
        pState->hBtnOk = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_OK),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            190, 182, 92, 28,
            hDlg, (HMENU)IDOK, hInst, NULL
        );
        SendMessageW(pState->hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        pState->hBtnCancel = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_CANCEL),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            288, 182, 92, 28,
            hDlg, (HMENU)IDCANCEL, hInst, NULL
        );
        SendMessageW(pState->hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Adaptation UxTheme sombre pour les contrôles du dialogue
        HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
        if (hUxTheme) {
            typedef HRESULT (WINAPI *pfnSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
            pfnSetWindowTheme pSetWindowTheme = (pfnSetWindowTheme)(void*)GetProcAddress(hUxTheme, "SetWindowTheme");
            if (pSetWindowTheme) {
                pSetWindowTheme(pState->hComboLang, g_bDarkModeActive ? L"DarkMode_CFD" : L"Explorer", NULL);
                if (g_bDarkModeActive) {
                    // Permet au texte des cases à cocher de respecter WM_CTLCOLORSTATIC en mode sombre
                    pSetWindowTheme(pState->hChkAuto, L"", L"");
                    pSetWindowTheme(pState->hChkHideWin, L"", L"");
                }
            }
            FreeLibrary(hUxTheme);
        }

        SetWindowSubclass(pState->hBtnOk, ButtonSubclassProc, 101, 0);
        SetWindowSubclass(pState->hBtnCancel, ButtonSubclassProc, 102, 0);
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_SET_COMBO_LANG && code == CBN_SELCHANGE) {
            UpdateActiveLangPreviewLabel(pState);
            return 0;
        }
        if (id == IDOK) {
            if (pState) {
                int selLang = (int)SendMessageW(pState->hComboLang, CB_GETCURSEL, 0, 0);
                if (selLang >= APP_LANG_AUTO && selLang <= APP_LANG_PT) {
                    g_nLanguageSetting = selLang;
                }

                BOOL newAuto = (SendMessageW(pState->hChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (newAuto != g_bAutoRefresh) {
                    g_bAutoRefresh = newAuto;
                    if (g_bAutoRefresh) {
                        SetTimer(pState->hParent, IDT_REFRESH_TIMER, REFRESH_INTERVAL_MS, NULL);
                    } else {
                        KillTimer(pState->hParent, IDT_REFRESH_TIMER);
                    }
                }

                g_bHideWindowsProcesses = (SendMessageW(pState->hChkHideWin, BM_GETCHECK, 0, 0) == BST_CHECKED);

                SaveSettings();
                ApplyLanguageToUI(pState->hParent);
                EnableWindow(pState->hParent, TRUE);
                pState->bDone = TRUE;
            }
            DestroyWindow(hDlg);
            return 0;
        }
        if (id == IDCANCEL) {
            if (pState) {
                EnableWindow(pState->hParent, TRUE);
                pState->bDone = TRUE;
            }
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        if (pState) {
            EnableWindow(pState->hParent, TRUE);
            pState->bDone = TRUE;
        }
        DestroyWindow(hDlg);
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hDlg, &rc);
        FillRect(hdc, &rc, g_bDarkModeActive ? g_hDarkWndBrush : GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdcCtrl = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdcCtrl, TRANSPARENT);
        if (g_bDarkModeActive) {
            if (pState && hCtrl == pState->hLblActiveLang) {
                SetTextColor(hdcCtrl, RGB(148, 163, 184));
            } else {
                SetTextColor(hdcCtrl, RGB(224, 228, 238));
            }
            return (LRESULT)g_hDarkWndBrush;
        } else {
            if (pState && hCtrl == pState->hLblActiveLang) {
                SetTextColor(hdcCtrl, RGB(90, 100, 115));
            } else {
                SetTextColor(hdcCtrl, GetSysColor(COLOR_WINDOWTEXT));
            }
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdcList = (HDC)wParam;
        if (g_bDarkModeActive) {
            SetTextColor(hdcList, RGB(242, 244, 248));
            SetBkColor(hdcList, RGB(42, 44, 52));
            return (LRESULT)g_hDarkEditBrush;
        }
        break;
    }

    case WM_DESTROY:
        if (pState) {
            if (pState->hBtnOk) RemoveWindowSubclass(pState->hBtnOk, ButtonSubclassProc, 101);
            if (pState->hBtnCancel) RemoveWindowSubclass(pState->hBtnCancel, ButtonSubclassProc, 102);
            pState->bDone = TRUE;
        }
        return 0;
    }

    return DefWindowProcW(hDlg, message, wParam, lParam);
}

void ShowSettingsDialog(HWND hParent) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    const WCHAR DLG_CLASS[] = L"TaskMgrSettingsDialog";

    WNDCLASSEXW wc = { 0 };
    if (!GetClassInfoExW(hInst, DLG_CLASS, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInst;
        wc.hIcon = g_hAppIcon;
        wc.hIconSm = g_hAppIconSm;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_bDarkModeActive ? g_hDarkWndBrush : (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = DLG_CLASS;
        RegisterClassExW(&wc);
    }

    SettingsDlgState state;
    memset(&state, 0, sizeof(state));
    state.hParent = hParent;

    int dlgW = 416;
    int dlgH = 262;
    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int x = rcParent.left + ((rcParent.right - rcParent.left) - dlgW) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - dlgH) / 2;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        DLG_CLASS,
        GetResString(IDS_SETTINGS_TITLE),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hParent, NULL, hInst, &state
    );

    if (!hDlg) {
        EnableWindow(hParent, TRUE);
        return;
    }

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    if (state.hComboLang) SetFocus(state.hComboLang);

    MSG msg;
    while (!state.bDone && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
}

// ============================================================================
// Fenêtre modale À propos (Version, Build, Date de compilation)
// ============================================================================
typedef struct {
    HWND hParent;
    HWND hLblSub;
    HWND hBtnClose;
    BOOL bDone;
} AboutDlgState;

static LRESULT CALLBACK AboutWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    AboutDlgState *pState = (AboutDlgState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (message) {
    case WM_CREATE: {
        CREATESTRUCTW *pCs = (CREATESTRUCTW*)lParam;
        pState = (AboutDlgState*)pCs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pState);

        HINSTANCE hInst = GetModuleHandleW(NULL);
        HFONT hFont = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hFontBold = g_hFontBold ? g_hFontBold : hFont;
        HFONT hFontSmall = g_hFontSmall ? g_hFontSmall : hFont;

        ApplyDwmModernAttributes(hDlg);
        if (g_pfnAllowDarkModeForWindow) {
            g_pfnAllowDarkModeForWindow(hDlg, g_bDarkModeActive);
        }

        // Icône applicative
        HWND hIconCtrl = CreateWindowExW(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            20, 20, 48, 48,
            hDlg, NULL, hInst, NULL
        );
        if (g_hAppIcon) {
            SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)g_hAppIcon, 0);
        }

        // Nom de l'application
        HWND hLblTitle = CreateWindowExW(
            0, L"STATIC", GetResString(IDS_ABOUT_APP_NAME),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            82, 20, 340, 22,
            hDlg, NULL, hInst, NULL
        );
        SendMessageW(hLblTitle, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        // Version + numéro de build
        WCHAR szVerText[128];
        swprintf_s(szVerText, sizeof(szVerText) / sizeof(WCHAR), GetResString(IDS_ABOUT_VERSION_FMT), APP_VERSION_FULL_W, APP_VER_BUILD);
        HWND hLblVer = CreateWindowExW(
            0, L"STATIC", szVerText,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            82, 44, 340, 18,
            hDlg, NULL, hInst, NULL
        );
        SendMessageW(hLblVer, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Date et heure de compilation
        WCHAR szDateW[64] = { 0 };
        WCHAR szTimeW[64] = { 0 };
        MultiByteToWideChar(CP_ACP, 0, __DATE__, -1, szDateW, 64);
        MultiByteToWideChar(CP_ACP, 0, __TIME__, -1, szTimeW, 64);

        WCHAR szCompileText[160];
        swprintf_s(szCompileText, sizeof(szCompileText) / sizeof(WCHAR), GetResString(IDS_ABOUT_COMPILE_FMT), szDateW, szTimeW);
        pState->hLblSub = CreateWindowExW(
            0, L"STATIC", szCompileText,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            82, 64, 340, 18,
            hDlg, NULL, hInst, NULL
        );
        SendMessageW(pState->hLblSub, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        // Plateforme / stack technique
        HWND hLblTech = CreateWindowExW(
            0, L"STATIC", GetResString(IDS_ABOUT_TECH),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 98, 404, 20,
            hDlg, NULL, hInst, NULL
        );
        SendMessageW(hLblTech, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Bouton Fermer
        pState->hBtnClose = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_CLOSE),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            324, 136, 100, 28,
            hDlg, (HMENU)IDOK, hInst, NULL
        );
        SendMessageW(pState->hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);
        SetWindowSubclass(pState->hBtnClose, ButtonSubclassProc, 103, 0);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            if (pState) {
                EnableWindow(pState->hParent, TRUE);
                pState->bDone = TRUE;
            }
            DestroyWindow(hDlg);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (pState) {
            EnableWindow(pState->hParent, TRUE);
            pState->bDone = TRUE;
        }
        DestroyWindow(hDlg);
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hDlg, &rc);
        FillRect(hdc, &rc, g_bDarkModeActive ? g_hDarkWndBrush : GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcCtrl = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdcCtrl, TRANSPARENT);
        if (g_bDarkModeActive) {
            if (pState && hCtrl == pState->hLblSub) {
                SetTextColor(hdcCtrl, RGB(148, 163, 184));
            } else {
                SetTextColor(hdcCtrl, RGB(226, 230, 240));
            }
            return (LRESULT)g_hDarkWndBrush;
        } else {
            if (pState && hCtrl == pState->hLblSub) {
                SetTextColor(hdcCtrl, RGB(90, 100, 115));
            } else {
                SetTextColor(hdcCtrl, GetSysColor(COLOR_WINDOWTEXT));
            }
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }

    case WM_DESTROY:
        if (pState) {
            if (pState->hBtnClose) RemoveWindowSubclass(pState->hBtnClose, ButtonSubclassProc, 103);
            pState->bDone = TRUE;
        }
        return 0;
    }

    return DefWindowProcW(hDlg, message, wParam, lParam);
}

void ShowAboutDialog(HWND hParent) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    const WCHAR DLG_CLASS[] = L"TaskMgrAboutDialog";

    WNDCLASSEXW wc = { 0 };
    if (!GetClassInfoExW(hInst, DLG_CLASS, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = AboutWndProc;
        wc.hInstance = hInst;
        wc.hIcon = g_hAppIcon;
        wc.hIconSm = g_hAppIconSm;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_bDarkModeActive ? g_hDarkWndBrush : (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = DLG_CLASS;
        RegisterClassExW(&wc);
    }

    AboutDlgState state;
    memset(&state, 0, sizeof(state));
    state.hParent = hParent;

    int dlgW = 458;
    int dlgH = 214;
    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int x = rcParent.left + ((rcParent.right - rcParent.left) - dlgW) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - dlgH) / 2;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        DLG_CLASS,
        GetResString(IDS_ABOUT_TITLE),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hParent, NULL, hInst, &state
    );

    if (!hDlg) {
        EnableWindow(hParent, TRUE);
        return;
    }

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    if (state.hBtnClose) SetFocus(state.hBtnClose);

    MSG msg;
    while (!state.bDone && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
}

// Point d'entrée WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // 1. Forcer une instance unique de l'application via un Mutex nommé
    const WCHAR MUTEX_NAME[] = L"Local\\TaskManager_SingleInstance_Mutex_9D4F2";
    g_uMsgRestoreInstance = RegisterWindowMessageW(L"WM_TASKMGR_RESTORE_INSTANCE");

    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Une instance est déjà en cours d'exécution : réactiver sa fenêtre au premier plan
        HWND hExistingWnd = FindWindowW(L"Win32NativeTaskManager", NULL);
        if (hExistingWnd) {
            if (g_uMsgRestoreInstance != 0) {
                PostMessageW(hExistingWnd, g_uMsgRestoreInstance, 0, 0);
            }
            if (IsIconic(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_RESTORE);
            } else {
                ShowWindow(hExistingWnd, SW_SHOW);
            }
            SetForegroundWindow(hExistingWnd);
        }
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 0; // Quitter immédiatement pour empêcher la duplication
    }
    g_hSingleInstanceMutex = hMutex;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    EnableDebugPrivilege();

    // Chargement préalable des réglages et exclusions depuis settings.ini
    LoadSettings();

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        g_pfnNtQueryInfoProcess = (pfnNtQueryInformationProcess)(void*)GetProcAddress(hNtdll, "NtQueryInformationProcess");
    }

    // Chargement de l'icône Task Manager (depuis la ressource PE ou via génération programmatique native)
    g_hAppIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_hAppIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (!g_hAppIcon) {
        g_hAppIcon = CreateTaskManagerAppIcon(32);
    }
    if (!g_hAppIconSm) {
        g_hAppIconSm = CreateTaskManagerAppIcon(16);
    }

    InitDarkModeAPIs();
    g_bDarkModeActive = IsWindowsDarkModeActive();
    InitThemeBrushes();

    const WCHAR CLASS_NAME[] = L"Win32NativeTaskManager";
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_hAppIcon ? g_hAppIcon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_bDarkModeActive ? g_hDarkWndBrush : (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = g_hAppIconSm ? g_hAppIconSm : LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        GetResString(IDS_APP_TITLE),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1260, 720,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        return 1;
    }

    g_hMainWnd = hWnd;
    ApplyDwmModernAttributes(hWnd);

    // Définition explicite des icônes de fenêtre (barre de titre, Alt+Tab, barre des tâches Windows)
    if (g_hAppIcon) {
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)g_hAppIcon);
    }
    if (g_hAppIconSm) {
        SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hAppIconSm);
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    InitGpuTelemetry();
    RefreshProcessList();

    if (g_bAutoRefresh) {
        SetTimer(hWnd, IDT_REFRESH_TIMER, REFRESH_INTERVAL_MS, NULL);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_DELETE) {
            if (GetFocus() == g_hListView) {
                TerminateSelectedProcessImmediately();
                continue;
            }
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5) {
            RefreshProcessList();
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

// Procédure de fenêtre principale (WndProc)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Réactivation inter-instance si l'utilisateur relance taskmgr.exe alors qu'il tourne déjà
    if (g_uMsgRestoreInstance != 0 && message == g_uMsgRestoreInstance) {
        if (g_bInTray) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            g_bInTray = FALSE;
        } else {
            if (IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_RESTORE);
            } else {
                ShowWindow(hWnd, SW_SHOW);
            }
            SetForegroundWindow(hWnd);
        }
        RefreshProcessList();
        return 0;
    }

    // Recréation automatique des icônes Tray lorsqu'Explorer redémarre après un crash
    if (g_uMsgTaskbarCreated != 0 && message == g_uMsgTaskbarCreated) {
        RecreateTrayIcons(TRUE);
        return 0;
    }

    switch (message) {
    case WM_CREATE: {
        g_hMainWnd = hWnd;
        g_uMsgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        AllowMessageThroughUIPI(hWnd, g_uMsgTaskbarCreated);
        AllowMessageThroughUIPI(hWnd, g_uMsgRestoreInstance);
        // Récupération de la police officielle Windows Segoe UI via les métriques système (au lieu de MS Sans Serif / 98)
        NONCLIENTMETRICSW ncm = { 0 };
        ncm.cbSize = sizeof(NONCLIENTMETRICSW);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0)) {
            g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        }
        if (!g_hFont) {
            g_hFont = CreateFontW(
                -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
            );
        }
        if (!g_hFont) {
            g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }
        g_hFontBold = CreateFontW(
            -13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        g_hFontSmall = CreateFontW(
            -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        HFONT hFont = g_hFont;

        // 1. Bouton Auto-Refresh (reprend l'état chargé de settings.ini)
        g_hBtnAutoRefresh = CreateWindowExW(
            0, L"BUTTON",
            g_bAutoRefresh ? GetResString(IDS_BTN_AUTOREFRESH_ON) : GetResString(IDS_BTN_AUTOREFRESH_OFF),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 150, 28,
            hWnd, (HMENU)IDC_BTN_AUTOREFRESH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnAutoRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 2. Bouton Actualiser
        g_hBtnRefresh = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_REFRESH),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            165, 10, 115, 28,
            hWnd, (HMENU)IDC_BTN_REFRESH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 3. Toggle pour Masquer les Processus Windows de Base (décalé pour combler l'espace du bouton Suppr)
        g_hBtnHideWindows = CreateWindowExW(
            0, L"BUTTON",
            g_bHideWindowsProcesses ? GetResString(IDS_BTN_WINDOWS_OFF) : GetResString(IDS_BTN_WINDOWS_ON),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            285, 10, 145, 28,
            hWnd, (HMENU)IDC_BTN_HIDE_WINDOWS, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnHideWindows, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 4. Bouton Gestion des Exclusions
        WCHAR szExclText[64];
        wsprintfW(szExclText, GetResString(IDS_BTN_EXCLUSIONS_FMT), g_ExcludedCount);
        g_hBtnExclusions = CreateWindowExW(
            0, L"BUTTON", szExclText,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            435, 10, 135, 28,
            hWnd, (HMENU)IDC_BTN_EXCLUSIONS, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnExclusions, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 5. Recherche / Filtre
        g_hStaticSearch = CreateWindowExW(
            0, L"STATIC", GetResString(IDS_LABEL_FILTER),
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            575, 14, 55, 20,
            hWnd, (HMENU)IDC_STATIC_SEARCH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hStaticSearch, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hEditSearch = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            635, 12, 180, 24,
            hWnd, (HMENU)IDC_EDIT_SEARCH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hEditSearch, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Boutons Paramètres et À propos alignés à droite de la barre d'outils
        g_hBtnSettings = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_SETTINGS),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1003, 10, 120, 28,
            hWnd, (HMENU)IDC_BTN_SETTINGS, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnSettings, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hBtnAbout = CreateWindowExW(
            0, L"BUTTON", GetResString(IDS_BTN_ABOUT),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1129, 10, 105, 28,
            hWnd, (HMENU)IDC_BTN_ABOUT, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnAbout, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 6. Bandeau graphique des performances (CPU, Mémoire RAM, GPU) au-dessus des processus
        g_hGraphBanner = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            10, 46, 1220, 90,
            hWnd, (HMENU)IDC_GRAPH_BANNER, GetModuleHandle(NULL), NULL
        );
        SetWindowSubclass(g_hGraphBanner, GraphBannerSubclassProc, 30, 0);

        // 7. Contrôle ListView SysListView32 avec bordure native subtile
        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            10, 144, 1220, 500,
            hWnd, (HMENU)IDC_LISTVIEW, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hListView, WM_SETFONT, (WPARAM)hFont, TRUE);

        ListView_SetExtendedListViewStyle(
            g_hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER
        );

        InitListViewColumns(g_hListView);

        // 8. Barre d'état StatusBar
        g_hStatusBar = CreateWindowExW(
            0, STATUSCLASSNAMEW, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_STATUSBAR, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hStatusBar, WM_SETFONT, (WPARAM)hFont, TRUE);

        int statusParts[5] = { 200, 360, 580, 840, -1 };
        SendMessage(g_hStatusBar, SB_SETPARTS, 5, (LPARAM)statusParts);

        // Subclass pour le rendu moderne sombre des colonnes et de l'en-tête (zéro colonnes noires)
        SetWindowSubclass(g_hListView, ListViewSubclassProc, 1, 0);
        HWND hHeader = ListView_GetHeader(g_hListView);
        if (hHeader) {
            SendMessage(hHeader, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetWindowSubclass(hHeader, HeaderSubclassProc, 2, 0);
        }

        // Subclass des boutons pour un rendu sombre mat, feutré et non éblouissant
        SetWindowSubclass(g_hBtnAutoRefresh, ButtonSubclassProc, 10, 0);
        SetWindowSubclass(g_hBtnRefresh, ButtonSubclassProc, 11, 0);
        SetWindowSubclass(g_hBtnHideWindows, ButtonSubclassProc, 13, 0);
        SetWindowSubclass(g_hBtnExclusions, ButtonSubclassProc, 14, 0);
        SetWindowSubclass(g_hBtnSettings, ButtonSubclassProc, 15, 0);
        SetWindowSubclass(g_hBtnAbout, ButtonSubclassProc, 16, 0);

        // Subclass de la barre d'état pour adoucir la luminosité et supprimer les bordures blanches 3D
        SetWindowSubclass(g_hStatusBar, StatusBarSubclassProc, 20, 0);

        // Application du thème visuel moderne Explorer et Dark Mode pour tous les contrôles
        ApplyModernWindowsTheme(hWnd, g_hListView);

        return 0;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        LayoutToolbarRightButtons(width);

        SendMessage(g_hStatusBar, WM_SIZE, 0, 0);
        RECT rcStatus;
        GetWindowRect(g_hStatusBar, &rcStatus);
        int statusHeight = rcStatus.bottom - rcStatus.top;

        int bannerTop = 44;
        int bannerHeight = 90;
        if (g_hGraphBanner) {
            MoveWindow(g_hGraphBanner, 10, bannerTop, width - 20, bannerHeight, TRUE);
        }

        int listTop = bannerTop + bannerHeight + 8;
        int listHeight = height - listTop - statusHeight - 4;
        if (listHeight < 50) listHeight = 50;

        MoveWindow(g_hListView, 10, listTop, width - 20, listHeight, TRUE);
        return 0;
    }

    case WM_CLOSE:
        // Lors de la fermeture, réduire l'application en System Tray au lieu de la quitter
        ShowWindow(hWnd, SW_HIDE);
        g_bInTray = TRUE;
        RefreshProcessList();
        return 0;

    case WM_TRAYICON: {
        UINT uTrayId = (UINT)wParam;
        // Simple clic (ou double-clic) gauche sur n'importe laquelle des 3 icônes pour réafficher l'application
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            if (IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_RESTORE);
            } else {
                ShowWindow(hWnd, SW_SHOW);
            }
            SetForegroundWindow(hWnd);
            g_bInTray = FALSE;
        } else if (lParam == WM_RBUTTONUP) {
            // Clic droit pour menu contextuel du Tray avec détails de la ressource cliquée
            POINT pt;
            GetCursorPos(&pt);
            HMENU hTrayMenu = CreatePopupMenu();

            WCHAR szHeader[128];
            if (uTrayId == TRAY_ID_CPU) {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), GetResString(IDS_TRAY_CPU_FMT), g_LastCpuPct);
            } else if (uTrayId == TRAY_ID_RAM) {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), GetResString(IDS_TRAY_RAM_FMT),
                    g_LastMemPct, g_LastUsedRamGb, g_LastTotalRamGb);
            } else {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), GetResString(IDS_TRAY_GPU_FMT), g_LastGpuPct);
            }

            AppendMenuW(hTrayMenu, MF_STRING | MF_GRAYED, 0, szHeader);
            AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hTrayMenu, MF_STRING, IDM_TRAY_OPEN, GetResString(IDS_TRAY_OPEN));
            SetMenuDefaultItem(hTrayMenu, IDM_TRAY_OPEN, FALSE);
            AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hTrayMenu, MF_STRING, IDM_TRAY_EXIT, GetResString(IDS_TRAY_EXIT));

            SetForegroundWindow(hWnd);
            TrackPopupMenu(hTrayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hTrayMenu);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD controlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        switch (controlId) {
        case IDM_TRAY_OPEN:
            if (IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_RESTORE);
            } else {
                ShowWindow(hWnd, SW_SHOW);
            }
            SetForegroundWindow(hWnd);
            g_bInTray = FALSE;
            break;

        case IDM_TRAY_EXIT:
            RemoveAllTrayIcons();
            DestroyWindow(hWnd);
            break;

        case IDC_BTN_REFRESH:
            RefreshProcessList();
            break;

        case IDC_BTN_AUTOREFRESH:
            g_bAutoRefresh = !g_bAutoRefresh;
            if (g_bAutoRefresh) {
                SetTimer(hWnd, IDT_REFRESH_TIMER, REFRESH_INTERVAL_MS, NULL);
                SetWindowTextW(g_hBtnAutoRefresh, GetResString(IDS_BTN_AUTOREFRESH_ON));
            } else {
                KillTimer(hWnd, IDT_REFRESH_TIMER);
                SetWindowTextW(g_hBtnAutoRefresh, GetResString(IDS_BTN_AUTOREFRESH_OFF));
            }
            SaveSettings();
            break;

        case IDC_BTN_HIDE_WINDOWS:
            g_bHideWindowsProcesses = !g_bHideWindowsProcesses;
            if (g_bHideWindowsProcesses) {
                SetWindowTextW(g_hBtnHideWindows, GetResString(IDS_BTN_WINDOWS_OFF));
            } else {
                SetWindowTextW(g_hBtnHideWindows, GetResString(IDS_BTN_WINDOWS_ON));
            }
            SaveSettings();
            RefreshProcessList();
            break;

        case IDC_BTN_SETTINGS:
            ShowSettingsDialog(hWnd);
            break;

        case IDC_BTN_ABOUT:
            ShowAboutDialog(hWnd);
            break;

        case IDC_BTN_EXCLUSIONS:
        case IDM_MANAGE_EXCLUSIONS: {
            HMENU hExclMenu = CreatePopupMenu();
            if (g_ExcludedCount == 0) {
                AppendMenuW(hExclMenu, MF_STRING | MF_GRAYED, 0, GetResString(IDS_MENU_NO_EXCLUSIONS));
            } else {
                AppendMenuW(hExclMenu, MF_STRING | MF_GRAYED, 0, GetResString(IDS_MENU_RESTORE_HEADER));
                for (int i = 0; i < g_ExcludedCount && i < 255; i++) {
                    WCHAR szMenuText[MAX_PATH + 32];
                    swprintf_s(szMenuText, sizeof(szMenuText) / sizeof(WCHAR), GetResString(IDS_MENU_RESTORE_ITEM_FMT), g_ExcludedProcesses[i]);
                    AppendMenuW(hExclMenu, MF_STRING, IDM_RESTORE_EXCLUSION_BASE + i, szMenuText);
                }
                AppendMenuW(hExclMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hExclMenu, MF_STRING, IDM_CLEAR_ALL_EXCLUSIONS, GetResString(IDS_MENU_CLEAR_EXCLUSIONS));
            }

            POINT pt;
            if (controlId == IDC_BTN_EXCLUSIONS && g_hBtnExclusions) {
                RECT rc;
                GetWindowRect(g_hBtnExclusions, &rc);
                pt.x = rc.left;
                pt.y = rc.bottom;
            } else {
                GetCursorPos(&pt);
            }

            TrackPopupMenu(hExclMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hExclMenu);
            break;
        }

        case IDM_CLEAR_ALL_EXCLUSIONS:
            ClearAllExcludedProcesses();
            SaveSettings();
            UpdateExclusionsButtonText();
            RefreshProcessList();
            break;

        case IDM_HIDE_BY_NAME: {
            int selectedIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (selectedIndex >= 0) {
                WCHAR szProcName[MAX_PATH] = { 0 };
                ListView_GetItemText(g_hListView, selectedIndex, COL_NAME, szProcName, MAX_PATH);
                if (szProcName[0] != L'\0') {
                    AddExcludedProcess(szProcName);
                    SaveSettings();
                    UpdateExclusionsButtonText();
                    RefreshProcessList();
                }
            }
            break;
        }

        case IDC_EDIT_SEARCH:
            if (notifyCode == EN_CHANGE) {
                GetWindowTextW(g_hEditSearch, g_szSearchFilter, sizeof(g_szSearchFilter) / sizeof(WCHAR));
                RefreshProcessList();
            }
            break;

        case IDM_KILL_IMMEDIATE:
            TerminateSelectedProcessImmediately();
            break;

        case IDM_PRIORITY_REALTIME:
            SetSelectedProcessPriority(REALTIME_PRIORITY_CLASS);
            break;

        case IDM_PRIORITY_HIGH:
            SetSelectedProcessPriority(HIGH_PRIORITY_CLASS);
            break;

        case IDM_PRIORITY_ABOVENORM:
            SetSelectedProcessPriority(ABOVE_NORMAL_PRIORITY_CLASS);
            break;

        case IDM_PRIORITY_NORMAL:
            SetSelectedProcessPriority(NORMAL_PRIORITY_CLASS);
            break;

        case IDM_PRIORITY_BELOWNORM:
            SetSelectedProcessPriority(BELOW_NORMAL_PRIORITY_CLASS);
            break;

        case IDM_PRIORITY_IDLE:
            SetSelectedProcessPriority(IDLE_PRIORITY_CLASS);
            break;

        case IDM_COPY_CMDLINE: {
            int selectedIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (selectedIndex >= 0) {
                WCHAR szCmd[2048] = { 0 };
                ListView_GetItemText(g_hListView, selectedIndex, COL_CMDLINE, szCmd, 2048);
                if (OpenClipboard(hWnd)) {
                    EmptyClipboard();
                    size_t bytes = (wcslen(szCmd) + 1) * sizeof(WCHAR);
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (hGlob) {
                        memcpy(GlobalLock(hGlob), szCmd, bytes);
                        GlobalUnlock(hGlob);
                        SetClipboardData(CF_UNICODETEXT, hGlob);
                    }
                    CloseClipboard();
                }
            }
            break;
        }

        case IDM_OPEN_LOCATION: {
            int selectedIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (selectedIndex >= 0) {
                LVITEMW item;
                item.mask = LVIF_PARAM;
                item.iItem = selectedIndex;
                item.iSubItem = 0;
                if (ListView_GetItem(g_hListView, &item)) {
                    DWORD pid = (DWORD)item.lParam;
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (hProc) {
                        WCHAR szPath[MAX_PATH] = { 0 };
                        DWORD size = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProc, 0, szPath, &size)) {
                            WCHAR szSelectParam[MAX_PATH + 32];
                            wsprintfW(szSelectParam, L"/select,\"%s\"", szPath);
                            ShellExecuteW(NULL, L"open", L"explorer.exe", szSelectParam, NULL, SW_SHOWNORMAL);
                        }
                        CloseHandle(hProc);
                    }
                }
            }
            break;
        }

        case IDM_TOGGLE_AUTO:
            SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_AUTOREFRESH, BN_CLICKED), (LPARAM)g_hBtnAutoRefresh);
            break;

        case IDM_TOGGLE_HIDE_WIN:
            SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_HIDE_WINDOWS, BN_CLICKED), (LPARAM)g_hBtnHideWindows);
            break;

        default:
            // Restauration d'un processus exclu individuel
            if (controlId >= IDM_RESTORE_EXCLUSION_BASE && controlId < IDM_RESTORE_EXCLUSION_BASE + MAX_EXCLUDED_PROCESSES) {
                int idx = controlId - IDM_RESTORE_EXCLUSION_BASE;
                if (idx < g_ExcludedCount) {
                    RemoveExcludedProcess(idx);
                    SaveSettings();
                    UpdateExclusionsButtonText();
                    RefreshProcessList();
                }
            }
            break;
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_REFRESH_TIMER) {
            RefreshProcessList();
        } else if (wParam == IDT_TRAY_RETRY_TIMER) {
            RecreateTrayIcons(FALSE);
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR lpnm = (LPNMHDR)lParam;
        if (lpnm->idFrom == IDC_LISTVIEW) {
            // RENDU PERSONNALISÉ DES LIGNES EN MODE SOMBRE (TEXTE BLANC & FOND SOMBRE LISIBLE)
            if (lpnm->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
                if (lplvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (lplvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    if (g_bDarkModeActive) {
                        // Alternance subtile de gris ardoise feutré pour une lisibilité optimale sans noir agressif
                        if ((lplvcd->nmcd.dwItemSpec % 2) == 1) {
                            lplvcd->clrTextBk = RGB(42, 44, 52);
                        } else {
                            lplvcd->clrTextBk = RGB(36, 38, 44);
                        }
                        lplvcd->clrText = RGB(242, 244, 248);
                    } else {
                        lplvcd->clrTextBk = RGB(255, 255, 255);
                        lplvcd->clrText = RGB(0, 0, 0);
                    }
                    return CDRF_NEWFONT;
                }
            }

            // CLIC SUR UN EN-TÊTE DE COLONNE -> TRI DES PROCESSUS
            if (lpnm->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                int clickedCol = pnmv->iSubItem;

                if (g_SortColumn == clickedCol) {
                    g_bSortAscending = !g_bSortAscending;
                } else {
                    g_SortColumn = clickedCol;
                    // Décroissant par défaut pour les chiffres (CPU, RAM, Disque, Réseau, GPU), croissant pour le texte
                    if (clickedCol == COL_NAME || clickedCol == COL_CMDLINE || clickedCol == COL_PRIORITY) {
                        g_bSortAscending = TRUE;
                    } else {
                        g_bSortAscending = FALSE;
                    }
                }

                UpdateHeaderSortArrow();
                SaveSettings();
                qsort(g_FilteredList, g_FilteredCount, sizeof(ProcessEntry*), ProcessCompare);
                RefreshProcessList();
                return 0;
            }

            // CLIC DROIT -> MENU CONTEXTUEL
            if (lpnm->code == NM_RCLICK) {
                LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
                if (lpnmitem->iItem >= 0) {
                    HMENU hPopup = CreatePopupMenu();
                    HMENU hSubPriority = CreatePopupMenu();

                    WCHAR szProcName[MAX_PATH] = { 0 };
                    ListView_GetItemText(g_hListView, lpnmitem->iItem, COL_NAME, szProcName, MAX_PATH);

                    AppendMenuW(hPopup, MF_STRING, IDM_KILL_IMMEDIATE, GetResString(IDS_MENU_KILL_IMMEDIATE));
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);

                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_REALTIME, GetResString(IDS_MENU_PRIORITY_REALTIME));
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_HIGH, GetResString(IDS_MENU_PRIORITY_HIGH));
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_ABOVENORM, GetResString(IDS_MENU_PRIORITY_ABOVENORM));
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_NORMAL, GetResString(IDS_MENU_PRIORITY_NORMAL));
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_BELOWNORM, GetResString(IDS_MENU_PRIORITY_BELOWNORM));
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_IDLE, GetResString(IDS_MENU_PRIORITY_IDLE));

                    AppendMenuW(hPopup, MF_POPUP, (UINT_PTR)hSubPriority, GetResString(IDS_MENU_SET_PRIORITY));
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hPopup, MF_STRING, IDM_COPY_CMDLINE, GetResString(IDS_MENU_COPY_CMDLINE));
                    AppendMenuW(hPopup, MF_STRING, IDM_OPEN_LOCATION, GetResString(IDS_MENU_OPEN_LOCATION));
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);

                    // Option pour masquer ce processus par son nom
                    if (szProcName[0] != L'\0') {
                        WCHAR szHideText[MAX_PATH + 64];
                        swprintf_s(szHideText, sizeof(szHideText) / sizeof(WCHAR), GetResString(IDS_MENU_HIDE_BY_NAME_FMT), szProcName);
                        AppendMenuW(hPopup, MF_STRING, IDM_HIDE_BY_NAME, szHideText);
                    }
                    AppendMenuW(hPopup, MF_STRING, IDM_MANAGE_EXCLUSIONS, GetResString(IDS_MENU_MANAGE_EXCLUSIONS));

                    POINT pt;
                    GetCursorPos(&pt);
                    TrackPopupMenu(hPopup, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hSubPriority);
                    DestroyMenu(hPopup);
                }
            }
        }
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBr = g_bDarkModeActive ? g_hDarkWndBrush : GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rc, hBr);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        // Fond transparent pour le label 'Filtrer :' adapté au mode sombre feutré
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        if (g_bDarkModeActive) {
            SetTextColor(hdcStatic, RGB(200, 203, 212));
            return (LRESULT)g_hDarkWndBrush;
        } else {
            SetTextColor(hdcStatic, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }

    case WM_CTLCOLOREDIT: {
        // Boîte de recherche adaptée au mode sombre feutré
        HDC hdcEdit = (HDC)wParam;
        if (g_bDarkModeActive) {
            SetTextColor(hdcEdit, RGB(242, 244, 248));
            SetBkColor(hdcEdit, RGB(42, 44, 52));
            return (LRESULT)g_hDarkEditBrush;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        // Réaction dynamique si l'utilisateur change de thème ou bascule en Dark Mode Windows
        ApplyModernWindowsTheme(hWnd, g_hListView);
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;

    case WM_DESTROY:
        if (g_hListView) {
            RemoveWindowSubclass(g_hListView, ListViewSubclassProc, 1);
            HWND hHeader = ListView_GetHeader(g_hListView);
            if (hHeader) {
                RemoveWindowSubclass(hHeader, HeaderSubclassProc, 2);
            }
        }
        if (g_hBtnAutoRefresh) RemoveWindowSubclass(g_hBtnAutoRefresh, ButtonSubclassProc, 10);
        if (g_hBtnRefresh)     RemoveWindowSubclass(g_hBtnRefresh, ButtonSubclassProc, 11);
        if (g_hBtnHideWindows) RemoveWindowSubclass(g_hBtnHideWindows, ButtonSubclassProc, 13);
        if (g_hBtnExclusions)  RemoveWindowSubclass(g_hBtnExclusions, ButtonSubclassProc, 14);
        if (g_hBtnSettings)    RemoveWindowSubclass(g_hBtnSettings, ButtonSubclassProc, 15);
        if (g_hBtnAbout)       RemoveWindowSubclass(g_hBtnAbout, ButtonSubclassProc, 16);
        if (g_hStatusBar)      RemoveWindowSubclass(g_hStatusBar, StatusBarSubclassProc, 20);
        if (g_hGraphBanner)    RemoveWindowSubclass(g_hGraphBanner, GraphBannerSubclassProc, 30);

        FreeThemeBrushes();
        RemoveAllTrayIcons();
        if (g_hAppIcon) {
            DestroyIcon(g_hAppIcon);
            g_hAppIcon = NULL;
        }
        if (g_hAppIconSm) {
            DestroyIcon(g_hAppIconSm);
            g_hAppIconSm = NULL;
        }
        if (g_hFont) {
            DeleteObject(g_hFont);
            g_hFont = NULL;
        }
        if (g_hFontBold) {
            DeleteObject(g_hFontBold);
            g_hFontBold = NULL;
        }
        if (g_hFontSmall) {
            DeleteObject(g_hFontSmall);
            g_hFontSmall = NULL;
        }
        if (g_hSingleInstanceMutex) {
            CloseHandle(g_hSingleInstanceMutex);
            g_hSingleInstanceMutex = NULL;
        }
        SaveSettings();
        ShutdownGpuTelemetry();
        KillTimer(hWnd, IDT_TRAY_RETRY_TIMER);
        if (g_bAutoRefresh) {
            KillTimer(hWnd, IDT_REFRESH_TIMER);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// Initialisation des colonnes de la ListView
void InitListViewColumns(HWND hListView) {
    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;

    int colWidths[COL_COUNT] = { 200, 75, 80, 105, 110, 110, 80, 120, 420 };
    int colFmts[COL_COUNT] = {
        LVCFMT_LEFT, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_RIGHT,
        LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_LEFT, LVCFMT_LEFT
    };

    for (int i = 0; i < COL_COUNT; i++) {
        lvc.iSubItem = i;
        lvc.cx = colWidths[i];
        lvc.fmt = colFmts[i];
        lvc.pszText = (LPWSTR)GetColumnTitle(i);
        ListView_InsertColumn(hListView, i, &lvc);
    }

    UpdateHeaderSortArrow();
}

// Mise à jour de l'indicateur visuel de tri dans l'en-tête (symbole ▲ / ▼ + style HDF_SORT)
void UpdateHeaderSortArrow(void) {
    if (!g_hListView) return;

    // 1. Mise à jour fiable du libellé de chaque colonne via l'API ListView officielle
    for (int i = 0; i < COL_COUNT; i++) {
        WCHAR szTitle[128];
        if (i == g_SortColumn) {
            // Symbole visuel explicite directement dans le texte de la colonne triée
            wsprintfW(szTitle, L"%s %s", GetColumnTitle(i), g_bSortAscending ? L"▲" : L"▼");
        } else {
            wcscpy_s(szTitle, sizeof(szTitle)/sizeof(WCHAR), GetColumnTitle(i));
        }

        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT;
        lvc.pszText = szTitle;
        ListView_SetColumn(g_hListView, i, &lvc);
    }

    // 2. Mise à jour des drapeaux graphiques HDF_SORTUP / HDF_SORTDOWN sur le header
    HWND hHeader = ListView_GetHeader(g_hListView);
    if (hHeader) {
        for (int i = 0; i < COL_COUNT; i++) {
            HDITEMW hdi = { 0 };
            hdi.mask = HDI_FORMAT;
            if (Header_GetItem(hHeader, i, &hdi)) {
                hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
                if (i == g_SortColumn) {
                    hdi.fmt |= g_bSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
                }
                Header_SetItem(hHeader, i, &hdi);
            }
        }
    }
}

// Rangs numériques pour le tri de priorité
static int PriorityRank(DWORD prio) {
    switch (prio) {
    case REALTIME_PRIORITY_CLASS:     return 6;
    case HIGH_PRIORITY_CLASS:         return 5;
    case ABOVE_NORMAL_PRIORITY_CLASS: return 4;
    case NORMAL_PRIORITY_CLASS:       return 3;
    case BELOW_NORMAL_PRIORITY_CLASS: return 2;
    case IDLE_PRIORITY_CLASS:         return 1;
    default:                          return 0;
    }
}

// Comparateur de tri multi-colonnes pour qsort
int ProcessCompare(const void *a, const void *b) {
    const ProcessEntry *pA = *(const ProcessEntry**)a;
    const ProcessEntry *pB = *(const ProcessEntry**)b;

    int diff = 0;
    switch (g_SortColumn) {
    case COL_NAME:
        diff = _wcsicmp(pA->szName, pB->szName);
        break;
    case COL_PID:
        if (pA->pid < pB->pid) diff = -1;
        else if (pA->pid > pB->pid) diff = 1;
        break;
    case COL_CPU:
        if (pA->cpuPercent < pB->cpuPercent) diff = -1;
        else if (pA->cpuPercent > pB->cpuPercent) diff = 1;
        break;
    case COL_RAM:
        if (pA->workingSetMB < pB->workingSetMB) diff = -1;
        else if (pA->workingSetMB > pB->workingSetMB) diff = 1;
        break;
    case COL_DISK:
        if (pA->diskMBPerSec < pB->diskMBPerSec) diff = -1;
        else if (pA->diskMBPerSec > pB->diskMBPerSec) diff = 1;
        break;
    case COL_NET:
        if (pA->netKBPerSec < pB->netKBPerSec) diff = -1;
        else if (pA->netKBPerSec > pB->netKBPerSec) diff = 1;
        break;
    case COL_GPU:
        if (pA->gpuPercent < pB->gpuPercent) diff = -1;
        else if (pA->gpuPercent > pB->gpuPercent) diff = 1;
        break;
    case COL_PRIORITY: {
        int rA = PriorityRank(pA->priorityClass);
        int rB = PriorityRank(pB->priorityClass);
        diff = rA - rB;
        break;
    }
    case COL_CMDLINE:
        diff = _wcsicmp(pA->szCmdLine, pB->szCmdLine);
        break;
    default:
        diff = 0;
        break;
    }

    return g_bSortAscending ? diff : -diff;
}

// Extraction propre des arguments CLI depuis le Process Environment Block (PEB)
void GetProcessCommandLine(HANDLE hProcess, WCHAR *pszCmdLine, DWORD maxChars) {
    pszCmdLine[0] = L'\0';

    if (!g_pfnNtQueryInfoProcess) {
        wcsncpy_s(pszCmdLine, maxChars, GetResString(IDS_STR_NO_NTAPI), _TRUNCATE);
        return;
    }

    PROCESS_BASIC_INFORMATION pbi;
    ULONG len = 0;
    NTSTATUS status = g_pfnNtQueryInfoProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &len);
    if (status != 0 || !pbi.PebBaseAddress) {
        wcsncpy_s(pszCmdLine, maxChars, GetResString(IDS_STR_RESTRICTED_ACCESS), _TRUNCATE);
        return;
    }

    PEB peb;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(peb), &bytesRead) || bytesRead == 0) {
        wcsncpy_s(pszCmdLine, maxChars, GetResString(IDS_STR_RESTRICTED_ACCESS), _TRUNCATE);
        return;
    }

    RTL_USER_PROCESS_PARAMETERS procParams;
    if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &procParams, sizeof(procParams), &bytesRead) || bytesRead == 0) {
        wcsncpy_s(pszCmdLine, maxChars, GetResString(IDS_STR_RESTRICTED_ACCESS), _TRUNCATE);
        return;
    }

    USHORT cmdLen = procParams.CommandLine.Length;
    if (cmdLen == 0 || !procParams.CommandLine.Buffer) {
        wcsncpy_s(pszCmdLine, maxChars, L"", _TRUNCATE);
        return;
    }

    DWORD toRead = cmdLen;
    if (toRead > (maxChars - 1) * sizeof(WCHAR)) {
        toRead = (maxChars - 1) * sizeof(WCHAR);
    }

    if (ReadProcessMemory(hProcess, procParams.CommandLine.Buffer, pszCmdLine, toRead, &bytesRead) && bytesRead > 0) {
        pszCmdLine[bytesRead / sizeof(WCHAR)] = L'\0';
    } else {
        wcsncpy_s(pszCmdLine, maxChars, GetResString(IDS_STR_RESTRICTED_ACCESS), _TRUNCATE);
    }
}

// Traduction de la classe de priorité en texte lisible
const WCHAR* PriorityClassToString(DWORD priorityClass) {
    switch (priorityClass) {
    case REALTIME_PRIORITY_CLASS:     return GetResString(IDS_PRIO_REALTIME);
    case HIGH_PRIORITY_CLASS:         return GetResString(IDS_PRIO_HIGH);
    case ABOVE_NORMAL_PRIORITY_CLASS: return GetResString(IDS_PRIO_ABOVENORM);
    case NORMAL_PRIORITY_CLASS:       return GetResString(IDS_PRIO_NORMAL);
    case BELOW_NORMAL_PRIORITY_CLASS: return GetResString(IDS_PRIO_BELOWNORM);
    case IDLE_PRIORITY_CLASS:         return GetResString(IDS_PRIO_IDLE);
    default:                          return GetResString(IDS_PRIO_UNKNOWN);
    }
}

// Mise à jour de la barre d'état avec métriques globales en temps réel
void UpdateStatusBar(int displayedCount, int totalCount, double totalCpu) {
    WCHAR szText[128];

    swprintf_s(szText, sizeof(szText) / sizeof(WCHAR), GetResString(IDS_STATUS_PROCESSES_FMT), displayedCount, totalCount);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)szText);

    swprintf_s(szText, sizeof(szText) / sizeof(WCHAR), GetResString(IDS_STATUS_CPU_FMT), totalCpu);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 1, (LPARAM)szText);

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        DWORD totalMB = (DWORD)(memStatus.ullTotalPhys / (1024 * 1024));
        DWORD usedMB  = (DWORD)((memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024 * 1024));
        swprintf_s(szText, sizeof(szText) / sizeof(WCHAR), GetResString(IDS_STATUS_RAM_FMT), usedMB, totalMB, memStatus.dwMemoryLoad);
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 2, (LPARAM)szText);
    }

    swprintf_s(szText, sizeof(szText) / sizeof(WCHAR), GetResString(IDS_STATUS_EXCLUSIONS_FMT),
        g_ExcludedCount,
        GetColumnTitle(g_SortColumn),
        g_bSortAscending ? L"▲" : L"▼"
    );
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 3, (LPARAM)szText);

    // SendMessageW(g_hStatusBar, SB_SETTEXTW, 4, (LPARAM)L"Fermer: Réduire en Tray | Suppr: Tuer");

    if (g_bDarkModeActive && g_hStatusBar) {
        InvalidateRect(g_hStatusBar, NULL, FALSE);
    }
}

// ============================================================================
// Télémétrie GPU réelle via PDH ("\GPU Engine(*)\Utilization Percentage")
// ============================================================================
static BOOL ParseGpuEngineInstanceName(const WCHAR *szName, DWORD *pOutPid, WCHAR *szEngineKey, size_t cchKey) {
    if (!szName || !pOutPid || !szEngineKey || cchKey == 0) return FALSE;
    szEngineKey[0] = L'\0';

    const WCHAR *pPid = wcsstr(szName, L"pid_");
    if (!pPid) return FALSE;

    WCHAR *pEnd = NULL;
    unsigned long pidVal = wcstoul(pPid + 4, &pEnd, 10);
    if (!pEnd || pEnd == pPid + 4) return FALSE;
    *pOutPid = (DWORD)pidVal;

    const WCHAR *pLuid = wcsstr(pEnd, L"luid_");
    const WCHAR *pStart = pLuid ? pLuid : ((*pEnd == L'_') ? (pEnd + 1) : pEnd);
    const WCHAR *pEngType = wcsstr(pStart, L"_engtype_");

    size_t len = pEngType ? (size_t)(pEngType - pStart) : wcslen(pStart);
    if (len == 0) {
        wcsncpy_s(szEngineKey, cchKey, L"default", _TRUNCATE);
    } else {
        if (len >= cchKey) len = cchKey - 1;
        memcpy(szEngineKey, pStart, len * sizeof(WCHAR));
        szEngineKey[len] = L'\0';
    }
    return TRUE;
}

void InitGpuTelemetry(void) {
    if (g_bGpuPdhReady) return;

    g_hPdhDll = LoadLibraryW(L"pdh.dll");
    if (!g_hPdhDll) return;

    g_pfnPdhOpenQueryW = (pfnPdhOpenQueryW)(void*)GetProcAddress(g_hPdhDll, "PdhOpenQueryW");
    g_pfnPdhAddEnglishCounterW = (pfnPdhAddEnglishCounterW)(void*)GetProcAddress(g_hPdhDll, "PdhAddEnglishCounterW");
    g_pfnPdhCollectQueryData = (pfnPdhCollectQueryData)(void*)GetProcAddress(g_hPdhDll, "PdhCollectQueryData");
    g_pfnPdhGetFormattedCounterArrayW = (pfnPdhGetFormattedCounterArrayW)(void*)GetProcAddress(g_hPdhDll, "PdhGetFormattedCounterArrayW");
    g_pfnPdhCloseQuery = (pfnPdhCloseQuery)(void*)GetProcAddress(g_hPdhDll, "PdhCloseQuery");

    if (!g_pfnPdhOpenQueryW || !g_pfnPdhAddEnglishCounterW || !g_pfnPdhCollectQueryData ||
        !g_pfnPdhGetFormattedCounterArrayW || !g_pfnPdhCloseQuery) {
        ShutdownGpuTelemetry();
        return;
    }

    if (g_pfnPdhOpenQueryW(NULL, 0, &g_hGpuQuery) != ERROR_SUCCESS || !g_hGpuQuery) {
        ShutdownGpuTelemetry();
        return;
    }

    if (g_pfnPdhAddEnglishCounterW(g_hGpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_hGpuCounter) != ERROR_SUCCESS || !g_hGpuCounter) {
        ShutdownGpuTelemetry();
        return;
    }

    g_bGpuPdhReady = TRUE;
    // Premier échantillon de référence (les compteurs de taux PDH exigent 2 collectes)
    if (g_pfnPdhCollectQueryData(g_hGpuQuery) == ERROR_SUCCESS) {
        g_bGpuBaselineCollected = TRUE;
        g_LastGpuCollectTick = GetTickCount64();
    }
}

void ShutdownGpuTelemetry(void) {
    if (g_hGpuQuery && g_pfnPdhCloseQuery) {
        g_pfnPdhCloseQuery(g_hGpuQuery);
    }
    g_hGpuQuery = NULL;
    g_hGpuCounter = NULL;
    g_bGpuPdhReady = FALSE;
    g_bGpuBaselineCollected = FALSE;

    if (g_pGpuPdhBuf) {
        free(g_pGpuPdhBuf);
        g_pGpuPdhBuf = NULL;
        g_GpuPdhBufCap = 0;
    }
    if (g_hPdhDll) {
        FreeLibrary(g_hPdhDll);
        g_hPdhDll = NULL;
    }
    g_pfnPdhOpenQueryW = NULL;
    g_pfnPdhAddEnglishCounterW = NULL;
    g_pfnPdhCollectQueryData = NULL;
    g_pfnPdhGetFormattedCounterArrayW = NULL;
    g_pfnPdhCloseQuery = NULL;
}

void CollectGpuTelemetry(ULONGLONG curTick) {
    if (!g_bGpuPdhReady || !g_hGpuQuery || !g_hGpuCounter) {
        g_CachedGpuProcCount = 0;
        g_CachedTotalGpuPct = 0.0;
        return;
    }

    // Éviter de rééchantillonner à <750ms lors des frappes rapides dans le filtre
    if (g_bGpuBaselineCollected && g_LastGpuCollectTick > 0 &&
        curTick >= g_LastGpuCollectTick &&
        (curTick - g_LastGpuCollectTick) < GPU_SAMPLE_MIN_INTERVAL_MS) {
        return;
    }

    if (g_pfnPdhCollectQueryData(g_hGpuQuery) != ERROR_SUCCESS) {
        return;
    }
    g_LastGpuCollectTick = curTick;

    if (!g_bGpuBaselineCollected) {
        g_bGpuBaselineCollected = TRUE;
        g_CachedGpuProcCount = 0;
        g_CachedTotalGpuPct = 0.0;
        return;
    }

    DWORD bufSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = g_pfnPdhGetFormattedCounterArrayW(g_hGpuCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, NULL);

    int attempts = 0;
    while ((DWORD)status == PDH_MORE_DATA && bufSize > 0 && attempts < 3) {
        attempts++;
        if (bufSize > g_GpuPdhBufCap) {
            BYTE *pNew = (BYTE*)realloc(g_pGpuPdhBuf, bufSize);
            if (!pNew) return;
            g_pGpuPdhBuf = pNew;
            g_GpuPdhBufCap = bufSize;
        }
        itemCount = 0;
        status = g_pfnPdhGetFormattedCounterArrayW(
            g_hGpuCounter,
            PDH_FMT_DOUBLE,
            &bufSize,
            &itemCount,
            (PPDH_FMT_COUNTERVALUE_ITEM_W)g_pGpuPdhBuf
        );
    }

    if (status != ERROR_SUCCESS || itemCount == 0 || !g_pGpuPdhBuf) {
        g_CachedGpuProcCount = 0;
        g_CachedTotalGpuPct = 0.0;
        return;
    }

    GpuEngineTotal engines[MAX_GPU_ENGINES];
    int engineCount = 0;

    GpuProcEnginePair *pairs = (GpuProcEnginePair*)calloc(MAX_GPU_PROC_ENGINE_PAIRS, sizeof(GpuProcEnginePair));
    if (!pairs) return;
    int pairCount = 0;

    PPDH_FMT_COUNTERVALUE_ITEM_W items = (PPDH_FMT_COUNTERVALUE_ITEM_W)g_pGpuPdhBuf;
    for (DWORD i = 0; i < itemCount; i++) {
        DWORD cstat = items[i].FmtValue.CStatus;
        if (cstat != PDH_CSTATUS_VALID_DATA && cstat != PDH_CSTATUS_NEW_DATA) {
            continue;
        }

        double val = items[i].FmtValue.doubleValue;
        if (isnan(val) || isinf(val) || val <= 0.0) continue;
        if (val > 100.0) val = 100.0;

        DWORD pid = 0;
        WCHAR szEngineKey[64];
        if (!ParseGpuEngineInstanceName(items[i].szName, &pid, szEngineKey, 64)) {
            continue;
        }

        // Trouver ou créer le moteur matériel (luid + phys + eng)
        int eIdx = -1;
        for (int e = 0; e < engineCount; e++) {
            if (wcscmp(engines[e].szEngineKey, szEngineKey) == 0) {
                eIdx = e;
                break;
            }
        }
        if (eIdx < 0) {
            if (engineCount >= MAX_GPU_ENGINES) continue;
            eIdx = engineCount++;
            wcsncpy_s(engines[eIdx].szEngineKey, 64, szEngineKey, _TRUNCATE);
            engines[eIdx].totalPercent = 0.0;
        }
        engines[eIdx].totalPercent += val;

        // Cumuler par couple (PID, moteur)
        int pIdx = -1;
        for (int p = 0; p < pairCount; p++) {
            if (pairs[p].pid == pid && pairs[p].engineIdx == eIdx) {
                pIdx = p;
                break;
            }
        }
        if (pIdx < 0) {
            if (pairCount < MAX_GPU_PROC_ENGINE_PAIRS) {
                pairs[pairCount].pid = pid;
                pairs[pairCount].engineIdx = eIdx;
                pairs[pairCount].percent = val;
                pairCount++;
            }
        } else {
            pairs[pIdx].percent += val;
        }
    }

    // Chaque processus affiche le maximum de ses moteurs GPU utilisés (comportement natif Windows)
    g_CachedGpuProcCount = 0;
    for (int p = 0; p < pairCount; p++) {
        double pVal = pairs[p].percent;
        if (pVal > 100.0) pVal = 100.0;

        int found = -1;
        for (int c = 0; c < g_CachedGpuProcCount; c++) {
            if (g_CachedGpuProcs[c].pid == pairs[p].pid) {
                found = c;
                break;
            }
        }
        if (found < 0) {
            if (g_CachedGpuProcCount < MAX_GPU_PROC_SAMPLES) {
                g_CachedGpuProcs[g_CachedGpuProcCount].pid = pairs[p].pid;
                g_CachedGpuProcs[g_CachedGpuProcCount].gpuPercent = pVal;
                g_CachedGpuProcCount++;
            }
        } else if (pVal > g_CachedGpuProcs[found].gpuPercent) {
            g_CachedGpuProcs[found].gpuPercent = pVal;
        }
    }

    // Utilisation GPU globale = maximum parmi tous les moteurs GPU matériels (plafonné à 100%)
    double maxEngine = 0.0;
    for (int e = 0; e < engineCount; e++) {
        double eVal = engines[e].totalPercent;
        if (eVal > 100.0) eVal = 100.0;
        if (eVal > maxEngine) maxEngine = eVal;
    }
    g_CachedTotalGpuPct = maxEngine;

    free(pairs);
}

double LookupCachedGpuPercent(DWORD pid) {
    for (int i = 0; i < g_CachedGpuProcCount; i++) {
        if (g_CachedGpuProcs[i].pid == pid) {
            return g_CachedGpuProcs[i].gpuPercent;
        }
    }
    return 0.0;
}

// Rafraîchissement différentiel optimisé SANS scintillement
void RefreshProcessList(void) {
    FILETIME idleTime, kernelTime, userTime;
    GetSystemTimes(&idleTime, &kernelTime, &userTime);

    ULONGLONG curSysIdle = FileTimeToUInt64(&idleTime);
    ULONGLONG curSysKernel = FileTimeToUInt64(&kernelTime);
    ULONGLONG curSysUser = FileTimeToUInt64(&userTime);

    ULONGLONG sysKernelDiff = (curSysKernel >= g_LastSysKernelTime) ? (curSysKernel - g_LastSysKernelTime) : 0;
    ULONGLONG sysUserDiff   = (curSysUser >= g_LastSysUserTime) ? (curSysUser - g_LastSysUserTime) : 0;
    ULONGLONG sysTotalDiff  = sysKernelDiff + sysUserDiff;

    ULONGLONG sysIdleDiff   = (curSysIdle >= g_LastSysIdleTime) ? (curSysIdle - g_LastSysIdleTime) : 0;
    double totalCpuPercent = 0.0;
    if (sysTotalDiff > 0) {
        totalCpuPercent = 100.0 * (1.0 - ((double)sysIdleDiff / (double)sysTotalDiff));
        if (totalCpuPercent < 0.0) totalCpuPercent = 0.0;
        if (totalCpuPercent > 100.0) totalCpuPercent = 100.0;
    }

    g_LastSysIdleTime = curSysIdle;
    g_LastSysKernelTime = curSysKernel;
    g_LastSysUserTime = curSysUser;

    ULONGLONG curTick = GetTickCount64();
    double timeDeltaSec = (g_LastRefreshTime > 0 && curTick > g_LastRefreshTime) ?
        ((double)(curTick - g_LastRefreshTime) / 1000.0) : 1.0;
    if (timeDeltaSec <= 0.0) timeDeltaSec = 1.0;
    g_LastRefreshTime = curTick;

    CollectGpuTelemetry(curTick);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return;
    }

    DWORD activePids[MAX_HISTORY_PROCESSES];
    int activeCount = 0;
    g_ProcessCount = 0;

    // 1. Collecte de tous les processus actuels
    do {
        DWORD pid = pe32.th32ProcessID;
        if (activeCount < MAX_HISTORY_PROCESSES) {
            activePids[activeCount++] = pid;
        }

        if (g_ProcessCount >= MAX_HISTORY_PROCESSES) break;
        ProcessEntry *proc = &g_Processes[g_ProcessCount++];
        memset(proc, 0, sizeof(ProcessEntry));

        proc->pid = pid;
        wcsncpy_s(proc->szName, MAX_PATH, pe32.szExeFile, _TRUNCATE);
        proc->priorityClass = NORMAL_PRIORITY_CLASS;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) {
            hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        }

        ProcessHistory *hist = FindOrCreateHistory(pid);

        if (hProcess) {
            // Chemin complet du binaire
            DWORD pathSize = MAX_PATH;
            QueryFullProcessImageNameW(hProcess, 0, proc->szFullPath, &pathSize);

            // CPU %
            FILETIME ftCreate, ftExit, ftKernel, ftUser;
            if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                ULONGLONG curProcKernel = FileTimeToUInt64(&ftKernel);
                ULONGLONG curProcUser   = FileTimeToUInt64(&ftUser);

                if (hist && hist->timestamp > 0 && sysTotalDiff > 0) {
                    ULONGLONG procKernelDiff = (curProcKernel >= hist->kernelTime) ? (curProcKernel - hist->kernelTime) : 0;
                    ULONGLONG procUserDiff   = (curProcUser >= hist->userTime) ? (curProcUser - hist->userTime) : 0;
                    ULONGLONG procTotalDiff  = procKernelDiff + procUserDiff;

                    proc->cpuPercent = 100.0 * ((double)procTotalDiff / (double)sysTotalDiff);
                    if (proc->cpuPercent > 100.0) proc->cpuPercent = 100.0;
                }

                if (hist) {
                    hist->kernelTime = curProcKernel;
                    hist->userTime = curProcUser;
                }
            }

            // Mémoire (WorkingSetSize)
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                proc->workingSetMB = pmc.WorkingSetSize / (1024 * 1024);
            }

            // Disque et Réseau (IO Counters)
            IO_COUNTERS ioCounters;
            if (GetProcessIoCounters(hProcess, &ioCounters)) {
                if (hist && hist->timestamp > 0) {
                    ULONGLONG diskBytesDiff = 0;
                    if (ioCounters.ReadTransferCount >= hist->readBytes) {
                        diskBytesDiff += (ioCounters.ReadTransferCount - hist->readBytes);
                    }
                    if (ioCounters.WriteTransferCount >= hist->writeBytes) {
                        diskBytesDiff += (ioCounters.WriteTransferCount - hist->writeBytes);
                    }
                    proc->diskMBPerSec = ((double)diskBytesDiff / (1024.0 * 1024.0)) / timeDeltaSec;

                    ULONGLONG netBytesDiff = 0;
                    if (ioCounters.OtherTransferCount >= hist->otherBytes) {
                        netBytesDiff = ioCounters.OtherTransferCount - hist->otherBytes;
                    }
                    proc->netKBPerSec = ((double)netBytesDiff / 1024.0) / timeDeltaSec;
                }

                if (hist) {
                    hist->readBytes = ioCounters.ReadTransferCount;
                    hist->writeBytes = ioCounters.WriteTransferCount;
                    hist->otherBytes = ioCounters.OtherTransferCount;
                }
            }

            proc->priorityClass = GetPriorityClass(hProcess);
            GetProcessCommandLine(hProcess, proc->szCmdLine, sizeof(proc->szCmdLine) / sizeof(WCHAR));

            CloseHandle(hProcess);
        } else {
            wcsncpy_s(proc->szCmdLine, 1024, GetResString(IDS_STR_RESTRICTED), _TRUNCATE);
        }

        // GPU % réel (issu des compteurs WDDM PDH, indépendant des droits OpenProcess)
        proc->gpuPercent = LookupCachedGpuPercent(pid);

        // Détection de processus Windows de base
        proc->isWindowsBase = IsWindowsBaseProcess(pid, proc->szName, proc->szFullPath);

        if (hist) {
            hist->timestamp = curTick;
        }
    } while (Process32NextW(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    PruneHistory(activePids, activeCount);

    // GPU global réel : utilisation maximale parmi les moteurs matériels actifs (conforme au Gestionnaire Windows)
    double totalGpuPercent = g_CachedTotalGpuPct;
    if (totalGpuPercent < 0.0) totalGpuPercent = 0.0;
    if (totalGpuPercent > 100.0) totalGpuPercent = 100.0;

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    double memPercent = 0.0;
    double usedRamGb = 0.0;
    double totalRamGb = 0.0;
    if (GlobalMemoryStatusEx(&memStatus)) {
        memPercent = (double)memStatus.dwMemoryLoad;
        usedRamGb = (double)(memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        totalRamGb = (double)memStatus.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    }

    // Mise à jour des 3 icônes d'historique (CPU, RAM, GPU) dans la barre des tâches
    UpdateTrayIcons(totalCpuPercent, memPercent, totalGpuPercent, usedRamGb, totalRamGb);

    // 1. Détection du top consommateur pour CPU, RAM et GPU à cette seconde précise
    TopConsumer top1Cpu, top1Ram, top1Gpu;
    top1Cpu.szName[0] = top1Ram.szName[0] = top1Gpu.szName[0] = L'\0';
    top1Cpu.value = top1Ram.value = top1Gpu.value = -1.0;
    top1Cpu.pid = top1Ram.pid = top1Gpu.pid = 0;

    for (int i = 0; i < g_ProcessCount; i++) {
        if (g_Processes[i].pid == 0) continue; // Ignorer System Idle Process
        if (g_Processes[i].cpuPercent > top1Cpu.value) {
            top1Cpu.value = g_Processes[i].cpuPercent;
            top1Cpu.pid = g_Processes[i].pid;
            wcsncpy_s(top1Cpu.szName, 32, g_Processes[i].szName, _TRUNCATE);
        }
        if ((double)g_Processes[i].workingSetMB > top1Ram.value) {
            top1Ram.value = (double)g_Processes[i].workingSetMB;
            top1Ram.pid = g_Processes[i].pid;
            wcsncpy_s(top1Ram.szName, 32, g_Processes[i].szName, _TRUNCATE);
        }
        if (g_Processes[i].gpuPercent > 0.0 && g_Processes[i].gpuPercent > top1Gpu.value) {
            top1Gpu.value = g_Processes[i].gpuPercent;
            top1Gpu.pid = g_Processes[i].pid;
            wcsncpy_s(top1Gpu.szName, 32, g_Processes[i].szName, _TRUNCATE);
        }
    }

    // 2. Mise à jour de l'historique 1 heure (3600 secondes) pour les graphiques CPU / RAM / GPU du bandeau
    if (g_PerfHistoryCount == 0) {
        for (int i = 0; i < PERF_HISTORY_POINTS; i++) {
            g_PerfCpuHistory[i] = totalCpuPercent;
            g_PerfRamHistory[i] = memPercent;
            g_PerfGpuHistory[i] = totalGpuPercent;

            wcsncpy_s(g_TopCpuHistory[i].szName, 32, top1Cpu.szName, _TRUNCATE);
            g_TopCpuHistory[i].pid = top1Cpu.pid;
            g_TopCpuHistory[i].usage = (float)(top1Cpu.value > 0.0 ? top1Cpu.value : 0.0);

            wcsncpy_s(g_TopRamHistory[i].szName, 32, top1Ram.szName, _TRUNCATE);
            g_TopRamHistory[i].pid = top1Ram.pid;
            g_TopRamHistory[i].usage = (float)(top1Ram.value > 0.0 ? top1Ram.value : 0.0);

            wcsncpy_s(g_TopGpuHistory[i].szName, 32, top1Gpu.szName, _TRUNCATE);
            g_TopGpuHistory[i].pid = top1Gpu.pid;
            g_TopGpuHistory[i].usage = (float)(top1Gpu.value > 0.0 ? top1Gpu.value : 0.0);
        }
        g_PerfHistoryCount = PERF_HISTORY_POINTS;
    } else {
        memmove(&g_PerfCpuHistory[0], &g_PerfCpuHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(double));
        memmove(&g_PerfRamHistory[0], &g_PerfRamHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(double));
        memmove(&g_PerfGpuHistory[0], &g_PerfGpuHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(double));

        memmove(&g_TopCpuHistory[0], &g_TopCpuHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(TopProcSample));
        memmove(&g_TopRamHistory[0], &g_TopRamHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(TopProcSample));
        memmove(&g_TopGpuHistory[0], &g_TopGpuHistory[1], (PERF_HISTORY_POINTS - 1) * sizeof(TopProcSample));

        g_PerfCpuHistory[PERF_HISTORY_POINTS - 1] = totalCpuPercent;
        g_PerfRamHistory[PERF_HISTORY_POINTS - 1] = memPercent;
        g_PerfGpuHistory[PERF_HISTORY_POINTS - 1] = totalGpuPercent;

        wcsncpy_s(g_TopCpuHistory[PERF_HISTORY_POINTS - 1].szName, 32, top1Cpu.szName, _TRUNCATE);
        g_TopCpuHistory[PERF_HISTORY_POINTS - 1].pid = top1Cpu.pid;
        g_TopCpuHistory[PERF_HISTORY_POINTS - 1].usage = (float)(top1Cpu.value > 0.0 ? top1Cpu.value : 0.0);

        wcsncpy_s(g_TopRamHistory[PERF_HISTORY_POINTS - 1].szName, 32, top1Ram.szName, _TRUNCATE);
        g_TopRamHistory[PERF_HISTORY_POINTS - 1].pid = top1Ram.pid;
        g_TopRamHistory[PERF_HISTORY_POINTS - 1].usage = (float)(top1Ram.value > 0.0 ? top1Ram.value : 0.0);

        wcsncpy_s(g_TopGpuHistory[PERF_HISTORY_POINTS - 1].szName, 32, top1Gpu.szName, _TRUNCATE);
        g_TopGpuHistory[PERF_HISTORY_POINTS - 1].pid = top1Gpu.pid;
        g_TopGpuHistory[PERF_HISTORY_POINTS - 1].usage = (float)(top1Gpu.value > 0.0 ? top1Gpu.value : 0.0);
    }

    if (g_hGraphBanner && IsWindow(g_hGraphBanner)) {
        InvalidateRect(g_hGraphBanner, NULL, FALSE);
    }

    // 2. Filtrage dans la liste tampon (Recherche + Toggle processus Windows + Exclusions par nom)
    g_FilteredCount = 0;
    for (int i = 0; i < g_ProcessCount; i++) {
        // Filtrage des processus exclus par leur nom
        if (IsProcessExcluded(g_Processes[i].szName)) {
            continue;
        }

        // Toggle Masquer les processus Windows
        if (g_bHideWindowsProcesses && g_Processes[i].isWindowsBase) {
            continue;
        }

        // Filtrage textuel (Nom ou PID)
        if (g_szSearchFilter[0] != L'\0') {
            WCHAR szPidStr[32];
            wsprintfW(szPidStr, L"%u", g_Processes[i].pid);
            if (!ContainsSubstringI(g_Processes[i].szName, g_szSearchFilter) &&
                !ContainsSubstringI(szPidStr, g_szSearchFilter)) {
                continue;
            }
        }
        g_FilteredList[g_FilteredCount++] = &g_Processes[i];
    }

    // 3. Tri en mémoire via qsort
    qsort(g_FilteredList, g_FilteredCount, sizeof(ProcessEntry*), ProcessCompare);

    // 4. Synchronisation différentielle in-place SANS scintillement
    DWORD selectedPid = 0;
    int curSel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (curSel >= 0) {
        LVITEMW item;
        item.mask = LVIF_PARAM;
        item.iItem = curSel;
        item.iSubItem = 0;
        if (ListView_GetItem(g_hListView, &item)) {
            selectedPid = (DWORD)item.lParam;
        }
    }

    int currentItemCount = ListView_GetItemCount(g_hListView);
    int reselectIndex = -1;

    for (int i = 0; i < g_FilteredCount; i++) {
        ProcessEntry *p = g_FilteredList[i];
        if (p->pid == selectedPid) {
            reselectIndex = i;
        }

        WCHAR szPid[32], szCpu[32], szRam[32], szDisk[32], szNet[32], szGpu[32];
        wsprintfW(szPid, L"%u", p->pid);
        swprintf_s(szCpu, sizeof(szCpu)/sizeof(WCHAR), L"%.1f %%", p->cpuPercent);
        wsprintfW(szRam, GetResString(IDS_UNIT_RAM_FMT), (UINT)p->workingSetMB);
        swprintf_s(szDisk, sizeof(szDisk)/sizeof(WCHAR), GetResString(IDS_UNIT_DISK_FMT), p->diskMBPerSec);
        swprintf_s(szNet, sizeof(szNet)/sizeof(WCHAR), GetResString(IDS_UNIT_NET_FMT), p->netKBPerSec);
        swprintf_s(szGpu, sizeof(szGpu)/sizeof(WCHAR), L"%.1f %%", p->gpuPercent);
        const WCHAR *szPrio = PriorityClassToString(p->priorityClass);

        if (i < currentItemCount) {
            // Mise à jour sélective si le texte a changé (zéro scintillement)
            WCHAR curBuf[MAX_PATH];

            ListView_GetItemText(g_hListView, i, COL_NAME, curBuf, MAX_PATH);
            if (wcscmp(curBuf, p->szName) != 0) {
                ListView_SetItemText(g_hListView, i, COL_NAME, p->szName);
            }

            ListView_GetItemText(g_hListView, i, COL_PID, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szPid) != 0) {
                ListView_SetItemText(g_hListView, i, COL_PID, szPid);
            }

            ListView_GetItemText(g_hListView, i, COL_CPU, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szCpu) != 0) {
                ListView_SetItemText(g_hListView, i, COL_CPU, szCpu);
            }

            ListView_GetItemText(g_hListView, i, COL_RAM, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szRam) != 0) {
                ListView_SetItemText(g_hListView, i, COL_RAM, szRam);
            }

            ListView_GetItemText(g_hListView, i, COL_DISK, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szDisk) != 0) {
                ListView_SetItemText(g_hListView, i, COL_DISK, szDisk);
            }

            ListView_GetItemText(g_hListView, i, COL_NET, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szNet) != 0) {
                ListView_SetItemText(g_hListView, i, COL_NET, szNet);
            }

            ListView_GetItemText(g_hListView, i, COL_GPU, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szGpu) != 0) {
                ListView_SetItemText(g_hListView, i, COL_GPU, szGpu);
            }

            ListView_GetItemText(g_hListView, i, COL_PRIORITY, curBuf, MAX_PATH);
            if (wcscmp(curBuf, szPrio) != 0) {
                ListView_SetItemText(g_hListView, i, COL_PRIORITY, (LPWSTR)szPrio);
            }

            ListView_GetItemText(g_hListView, i, COL_CMDLINE, curBuf, MAX_PATH);
            if (wcscmp(curBuf, p->szCmdLine) != 0) {
                ListView_SetItemText(g_hListView, i, COL_CMDLINE, p->szCmdLine);
            }

            LVITEMW lvi;
            lvi.mask = LVIF_PARAM;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.lParam = (LPARAM)p->pid;
            ListView_SetItem(g_hListView, &lvi);
        } else {
            // Insertion d'un nouvel élément apparu
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            lvi.iSubItem = COL_NAME;
            lvi.pszText = p->szName;
            lvi.lParam = (LPARAM)p->pid;

            ListView_InsertItem(g_hListView, &lvi);
            ListView_SetItemText(g_hListView, i, COL_PID, szPid);
            ListView_SetItemText(g_hListView, i, COL_CPU, szCpu);
            ListView_SetItemText(g_hListView, i, COL_RAM, szRam);
            ListView_SetItemText(g_hListView, i, COL_DISK, szDisk);
            ListView_SetItemText(g_hListView, i, COL_NET, szNet);
            ListView_SetItemText(g_hListView, i, COL_GPU, szGpu);
            ListView_SetItemText(g_hListView, i, COL_PRIORITY, (LPWSTR)szPrio);
            ListView_SetItemText(g_hListView, i, COL_CMDLINE, p->szCmdLine);
        }
    }

    // Suppression des éléments superflus en fin de liste
    while (currentItemCount > g_FilteredCount) {
        currentItemCount--;
        ListView_DeleteItem(g_hListView, currentItemCount);
    }

    // Préservation de la sélection active
    if (reselectIndex >= 0) {
        ListView_SetItemState(g_hListView, reselectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    UpdateStatusBar(g_FilteredCount, g_ProcessCount, totalCpuPercent);
}

// Terminaison brutale immédiate sans confirmation (VK_DELETE ou bouton)
void TerminateSelectedProcessImmediately(void) {
    int selectedIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selectedIndex < 0) return;

    LVITEMW item;
    item.mask = LVIF_PARAM;
    item.iItem = selectedIndex;
    item.iSubItem = 0;

    if (!ListView_GetItem(g_hListView, &item)) return;

    DWORD pid = (DWORD)item.lParam;
    if (pid == 0 || pid == 4) {
        MessageBeep(MB_ICONERROR);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        // Terminaison brutale sans confirmation demandée par l'utilisateur
        TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        RefreshProcessList();
    } else {
        MessageBeep(MB_ICONWARNING);
    }
}

// Définition de la classe de priorité d'un processus
void SetSelectedProcessPriority(DWORD priorityClass) {
    int selectedIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selectedIndex < 0) return;

    LVITEMW item;
    item.mask = LVIF_PARAM;
    item.iItem = selectedIndex;
    item.iSubItem = 0;

    if (!ListView_GetItem(g_hListView, &item)) return;

    DWORD pid = (DWORD)item.lParam;
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (hProcess) {
        SetPriorityClass(hProcess, priorityClass);
        CloseHandle(hProcess);
        RefreshProcessList();
    } else {
        MessageBeep(MB_ICONWARNING);
    }
}
