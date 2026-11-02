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
 * - Métriques complètes : CPU %, GPU estimé, Mémoire (Mo), CLI Args, PID, Disque & Réseau
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
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
#define IDC_BTN_KILL         1005
#define IDC_EDIT_SEARCH      1006
#define IDC_STATIC_SEARCH    1007
#define IDC_BTN_PRIORITY     1008
#define IDC_BTN_HIDE_WINDOWS 1009
#define IDC_BTN_EXCLUSIONS   1010
#define IDC_GRAPH_BANNER     1011

// Identifiants du Timer
#define IDT_REFRESH_TIMER    2001
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

// Titres de base des colonnes
static const WCHAR* s_BaseColumnTitles[COL_COUNT] = {
    L"Nom de l'image",
    L"PID",
    L"CPU %",
    L"Mémoire (Mo)",
    L"Disque (Mo/s)",
    L"Réseau (Ko/s)",
    L"GPU %",
    L"Priorité",
    L"Ligne de commande (CLI Args)"
};

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

// Identifiants et état des 3 Tray Icons (CPU, RAM, GPU) avec historique
#define TRAY_ID_CPU 1
#define TRAY_ID_RAM 2
#define TRAY_ID_GPU 3

#define TRAY_HISTORY_POINTS 14 // 14 points d'historique temporels (colonnes de pixels x=1 à 14 dans 16x16)

static double g_TrayCpuHistory[TRAY_HISTORY_POINTS] = { 0 };
static double g_TrayRamHistory[TRAY_HISTORY_POINTS] = { 0 };
static double g_TrayGpuHistory[TRAY_HISTORY_POINTS] = { 0 };
static int g_TrayHistoryCount = 0;

// Historique étendu sur 60 secondes pour les graphiques de performances au-dessus des processus
#define PERF_HISTORY_POINTS 60
static double g_PerfCpuHistory[PERF_HISTORY_POINTS] = { 0 };
static double g_PerfRamHistory[PERF_HISTORY_POINTS] = { 0 };
static double g_PerfGpuHistory[PERF_HISTORY_POINTS] = { 0 };
static int g_PerfHistoryCount = 0;

static double g_LastCpuPct = 0.0;
static double g_LastMemPct = 0.0;
static double g_LastGpuPct = 0.0;
static double g_LastUsedRamGb = 0.0;
static double g_LastTotalRamGb = 0.0;

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
static HWND g_hBtnKill = NULL;
static HWND g_hBtnHideWindows = NULL;
static HWND g_hBtnExclusions = NULL;
static HWND g_hEditSearch = NULL;
static WCHAR g_szSearchFilter[128] = { 0 };
static HFONT g_hFont = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;

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
void RemoveAllTrayIcons(void);

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
        wsprintfW(szText, L"Exclusions (%d)", g_ExcludedCount);
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

// Mise à jour des 3 icônes distinctes d'historique (CPU, RAM, GPU) dans la barre des tâches
// avec affichage dynamique des 4 premiers consommateurs au survol de la souris
void UpdateTrayIcons(double cpuPct, double memPct, double gpuPct, double usedRamGb, double totalRamGb) {
    if (!g_hMainWnd) return;

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
            cpuPct,
            topCpu[0].szName[0] ? topCpu[0].szName : L"-", (topCpu[0].value > 0.0) ? topCpu[0].value : 0.0,
            topCpu[1].szName[0] ? topCpu[1].szName : L"-", (topCpu[1].value > 0.0) ? topCpu[1].value : 0.0,
            topCpu[2].szName[0] ? topCpu[2].szName : L"-", (topCpu[2].value > 0.0) ? topCpu[2].value : 0.0,
            topCpu[3].szName[0] ? topCpu[3].szName : L"-", (topCpu[3].value > 0.0) ? topCpu[3].value : 0.0);

        if (!g_bTrayCpuCreated) {
            if (Shell_NotifyIconW(NIM_ADD, &nid)) g_bTrayCpuCreated = TRUE;
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }

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
            L"RAM : %.1f %% (%.1f/%.1fGo)\n1. %.10s: %uMo\n2. %.10s: %uMo\n3. %.10s: %uMo\n4. %.10s: %uMo",
            memPct, usedRamGb, totalRamGb,
            topRam[0].szName[0] ? topRam[0].szName : L"-", (UINT)(topRam[0].value > 0.0 ? topRam[0].value : 0.0),
            topRam[1].szName[0] ? topRam[1].szName : L"-", (UINT)(topRam[1].value > 0.0 ? topRam[1].value : 0.0),
            topRam[2].szName[0] ? topRam[2].szName : L"-", (UINT)(topRam[2].value > 0.0 ? topRam[2].value : 0.0),
            topRam[3].szName[0] ? topRam[3].szName : L"-", (UINT)(topRam[3].value > 0.0 ? topRam[3].value : 0.0));

        if (!g_bTrayRamCreated) {
            if (Shell_NotifyIconW(NIM_ADD, &nid)) g_bTrayRamCreated = TRUE;
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }

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
            gpuPct,
            topGpu[0].szName[0] ? topGpu[0].szName : L"-", (topGpu[0].value > 0.0) ? topGpu[0].value : 0.0,
            topGpu[1].szName[0] ? topGpu[1].szName : L"-", (topGpu[1].value > 0.0) ? topGpu[1].value : 0.0,
            topGpu[2].szName[0] ? topGpu[2].szName : L"-", (topGpu[2].value > 0.0) ? topGpu[2].value : 0.0,
            topGpu[3].szName[0] ? topGpu[3].szName : L"-", (topGpu[3].value > 0.0) ? topGpu[3].value : 0.0);

        if (!g_bTrayGpuCreated) {
            if (Shell_NotifyIconW(NIM_ADD, &nid)) g_bTrayGpuCreated = TRUE;
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }

        if (g_hIconGpu) DestroyIcon(g_hIconGpu);
        g_hIconGpu = hNewGpu;
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

        LONG_PTR ctrlId = GetWindowLongPtrW(hWnd, GWLP_ID);
        BOOL isKill = (ctrlId == IDC_BTN_KILL);

        COLORREF clrBg, clrBorder, clrText;
        if (isKill) {
            // Teinte bordeaux sombre, douce et mate pour le bouton Tuer (sans éclat agressif)
            if (bPressed) {
                clrBg = RGB(50, 22, 25);
                clrBorder = RGB(105, 46, 52);
                clrText = RGB(240, 185, 190);
            } else if (bHover) {
                clrBg = RGB(75, 30, 35);
                clrBorder = RGB(125, 52, 58);
                clrText = RGB(245, 200, 205);
            } else {
                clrBg = RGB(60, 25, 28);
                clrBorder = RGB(92, 40, 45);
                clrText = RGB(225, 170, 175);
            }
        } else {
            // Boutons d'action standards : gris ardoise mat, contours adoucis, texte non éblouissant
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

// Dessin d'une carte de métrique (CPU, RAM, GPU) avec graphique d'historique 60s
static void DrawPerfCard(
    HDC hdc, RECT rcCard, const WCHAR *szTitle, const WCHAR *szSub,
    const double *history, int count, double curVal,
    COLORREF clrAccent, COLORREF clrPeak, COLORREF clrArea,
    BOOL bIsDark
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

    // 3. Cadre intérieur du graphique
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

    // Lignes de grille (50% horizontale)
    HPEN hGridPen = CreatePen(PS_SOLID, 1, bIsDark ? RGB(32, 35, 45) : RGB(235, 238, 243));
    hOldPen = (HPEN)SelectObject(hdc, hGridPen);
    int y50 = rcGraph.top + gH / 2;
    MoveToEx(hdc, rcGraph.left + 1, y50, NULL);
    LineTo(hdc, rcGraph.right - 1, y50);

    // Lignes verticales de grille (toutes les 15 secondes)
    for (int s = 1; s <= 3; s++) {
        int xGrid = rcGraph.left + (gW * s) / 4;
        MoveToEx(hdc, xGrid, rcGraph.top + 1, NULL);
        LineTo(hdc, xGrid, rcGraph.bottom - 1);
    }
    SelectObject(hdc, hOldPen);
    DeleteObject(hGridPen);

    // Tracé de la courbe et de la zone ombrée sous la courbe
    if (count > 1) {
        POINT *pts = (POINT*)malloc(sizeof(POINT) * (count + 2));
        if (pts) {
            for (int i = 0; i < count; i++) {
                double pct = history[i];
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;

                int px = rcGraph.left + (int)(((double)i / (double)(count - 1)) * (gW - 2)) + 1;
                int py = rcGraph.bottom - 1 - (int)((pct / 100.0) * (gH - 3));
                if (py < rcGraph.top + 1) py = rcGraph.top + 1;
                if (py > rcGraph.bottom - 1) py = rcGraph.bottom - 1;

                pts[i].x = px;
                pts[i].y = py;
            }

            // Polygone fermé pour le remplissage sous la courbe
            pts[count].x = pts[count - 1].x;
            pts[count].y = rcGraph.bottom - 1;
            pts[count + 1].x = pts[0].x;
            pts[count + 1].y = rcGraph.bottom - 1;

            HBRUSH hBrArea = CreateSolidBrush(clrArea);
            HPEN hPenNull = CreatePen(PS_NULL, 0, 0);
            hOldBr = (HBRUSH)SelectObject(hdc, hBrArea);
            hOldPen = (HPEN)SelectObject(hdc, hPenNull);
            Polygon(hdc, pts, count + 2);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hBrArea);
            DeleteObject(hPenNull);

            // Courbe principale
            HPEN hPenLine = CreatePen(PS_SOLID, 2, clrAccent);
            hOldPen = (HPEN)SelectObject(hdc, hPenLine);
            Polyline(hdc, pts, count);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPenLine);

            // Point lumineux final sur l'échantillon actuel
            int lastX = pts[count - 1].x;
            int lastY = pts[count - 1].y;
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
}

// Subclass pour le bandeau graphique des performances (CPU, RAM, GPU) au-dessus de la liste des processus
static LRESULT CALLBACK GraphBannerSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    if (uMsg == WM_ERASEBKGND) {
        return 1;
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
        WCHAR szCpuSub[64] = L"60 secondes • % d'utilisation";
        DrawPerfCard(
            hMemDC, rcCpu, L"CPU", szCpuSub,
            g_PerfCpuHistory, PERF_HISTORY_POINTS, g_LastCpuPct,
            RGB(16, 185, 129), RGB(52, 211, 153),
            g_bDarkModeActive ? RGB(10, 48, 38) : RGB(220, 252, 231),
            g_bDarkModeActive
        );

        // 2. Carte Mémoire RAM (Violet / Pourpre)
        RECT rcRam = { cardW + gap, 0, cardW * 2 + gap, h };
        WCHAR szRamSub[64];
        if (g_LastTotalRamGb > 0.0) {
            swprintf_s(szRamSub, sizeof(szRamSub) / sizeof(WCHAR), L"%.1f / %.1f Go (60s)", g_LastUsedRamGb, g_LastTotalRamGb);
        } else {
            wcsncpy_s(szRamSub, 64, L"Utilisation mémoire (60s)", _TRUNCATE);
        }
        DrawPerfCard(
            hMemDC, rcRam, L"Mémoire", szRamSub,
            g_PerfRamHistory, PERF_HISTORY_POINTS, g_LastMemPct,
            RGB(168, 85, 247), RGB(192, 132, 252),
            g_bDarkModeActive ? RGB(44, 24, 66) : RGB(243, 232, 255),
            g_bDarkModeActive
        );

        // 3. Carte GPU (Cyan)
        RECT rcGpu = { (cardW + gap) * 2, 0, w, h };
        WCHAR szGpuSub[64] = L"Moteur 3D • 60 secondes";
        DrawPerfCard(
            hMemDC, rcGpu, L"GPU", szGpuSub,
            g_PerfGpuHistory, PERF_HISTORY_POINTS, g_LastGpuPct,
            RGB(6, 182, 212), RGB(34, 211, 238),
            g_bDarkModeActive ? RGB(8, 44, 56) : RGB(207, 250, 254),
            g_bDarkModeActive
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
                            wsprintfW(szText, L"%s %s", s_BaseColumnTitles[iCol], g_bSortAscending ? L"▲" : L"▼");
                        } else {
                            wcsncpy_s(szText, 128, s_BaseColumnTitles[iCol], _TRUNCATE);
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
        if (g_hBtnKill) g_pfnAllowDarkModeForWindow(g_hBtnKill, g_bDarkModeActive);
        if (g_hBtnHideWindows) g_pfnAllowDarkModeForWindow(g_hBtnHideWindows, g_bDarkModeActive);
        if (g_hBtnExclusions) g_pfnAllowDarkModeForWindow(g_hBtnExclusions, g_bDarkModeActive);
        if (g_hEditSearch) g_pfnAllowDarkModeForWindow(g_hEditSearch, g_bDarkModeActive);
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

// Point d'entrée WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

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
        L"Gestionnaire des tâches Windows (C Win32 Natif)",
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

    RefreshProcessList();

    if (g_bAutoRefresh) {
        SetTimer(hWnd, IDT_REFRESH_TIMER, REFRESH_INTERVAL_MS, NULL);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_DELETE) {
            if (GetFocus() == g_hListView || GetFocus() == g_hBtnKill) {
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
    switch (message) {
    case WM_CREATE: {
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
            g_bAutoRefresh ? L"Auto-refresh: ACTIF (1s)" : L"Auto-refresh: PAUSE",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 160, 28,
            hWnd, (HMENU)IDC_BTN_AUTOREFRESH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnAutoRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 2. Bouton Actualiser
        g_hBtnRefresh = CreateWindowExW(
            0, L"BUTTON", L"Actualiser (F5)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            175, 10, 105, 28,
            hWnd, (HMENU)IDC_BTN_REFRESH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 3. Bouton Tuer Immédiatement
        g_hBtnKill = CreateWindowExW(
            0, L"BUTTON", L"Tuer Immédiatement (Suppr)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            285, 10, 175, 28,
            hWnd, (HMENU)IDC_BTN_KILL, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnKill, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 4. Toggle pour Masquer les Processus Windows de Base
        g_hBtnHideWindows = CreateWindowExW(
            0, L"BUTTON",
            g_bHideWindowsProcesses ? L"Processus Windows: MASQUÉS" : L"Processus Windows: AFFICHÉS",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            465, 10, 185, 28,
            hWnd, (HMENU)IDC_BTN_HIDE_WINDOWS, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnHideWindows, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 5. Bouton Gestion des Exclusions
        WCHAR szExclText[64];
        wsprintfW(szExclText, L"Exclusions (%d)", g_ExcludedCount);
        g_hBtnExclusions = CreateWindowExW(
            0, L"BUTTON", szExclText,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            655, 10, 135, 28,
            hWnd, (HMENU)IDC_BTN_EXCLUSIONS, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hBtnExclusions, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 6. Recherche / Filtre
        HWND hStatic = CreateWindowExW(
            0, L"STATIC", L"Filtrer :",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            795, 14, 50, 20,
            hWnd, (HMENU)IDC_STATIC_SEARCH, GetModuleHandle(NULL), NULL
        );
        SendMessage(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hEditSearch = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            850, 12, 180, 24,
            hWnd, (HMENU)IDC_EDIT_SEARCH, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hEditSearch, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 7. Bandeau graphique des performances (CPU, Mémoire RAM, GPU) au-dessus des processus
        g_hGraphBanner = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            10, 46, 1220, 90,
            hWnd, (HMENU)IDC_GRAPH_BANNER, GetModuleHandle(NULL), NULL
        );
        SetWindowSubclass(g_hGraphBanner, GraphBannerSubclassProc, 30, 0);

        // 8. Contrôle ListView SysListView32 avec bordure native subtile
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

        // 9. Barre d'état StatusBar
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
        SetWindowSubclass(g_hBtnKill, ButtonSubclassProc, 12, 0);
        SetWindowSubclass(g_hBtnHideWindows, ButtonSubclassProc, 13, 0);
        SetWindowSubclass(g_hBtnExclusions, ButtonSubclassProc, 14, 0);

        // Subclass de la barre d'état pour adoucir la luminosité et supprimer les bordures blanches 3D
        SetWindowSubclass(g_hStatusBar, StatusBarSubclassProc, 20, 0);

        // Application du thème visuel moderne Explorer et Dark Mode pour tous les contrôles
        ApplyModernWindowsTheme(hWnd, g_hListView);

        return 0;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

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
        // Double-clic gauche sur n'importe laquelle des 3 icônes pour réafficher l'application
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            g_bInTray = FALSE;
        } else if (lParam == WM_RBUTTONUP) {
            // Clic droit pour menu contextuel du Tray avec détails de la ressource cliquée
            POINT pt;
            GetCursorPos(&pt);
            HMENU hTrayMenu = CreatePopupMenu();

            WCHAR szHeader[128];
            if (uTrayId == TRAY_ID_CPU) {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), L"Historique CPU : %.1f %%", g_LastCpuPct);
            } else if (uTrayId == TRAY_ID_RAM) {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), L"Historique RAM : %.1f %% (%.1f / %.1f Go)",
                    g_LastMemPct, g_LastUsedRamGb, g_LastTotalRamGb);
            } else {
                swprintf_s(szHeader, sizeof(szHeader) / sizeof(WCHAR), L"Historique GPU : %.1f %%", g_LastGpuPct);
            }

            AppendMenuW(hTrayMenu, MF_STRING | MF_GRAYED, 0, szHeader);
            AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hTrayMenu, MF_STRING, IDM_TRAY_OPEN, L"Ouvrir le Task Manager");
            SetMenuDefaultItem(hTrayMenu, IDM_TRAY_OPEN, FALSE);
            AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hTrayMenu, MF_STRING, IDM_TRAY_EXIT, L"Quitter définitivement");

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
            ShowWindow(hWnd, SW_RESTORE);
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
                SetWindowTextW(g_hBtnAutoRefresh, L"Auto-refresh: ACTIF (1s)");
            } else {
                KillTimer(hWnd, IDT_REFRESH_TIMER);
                SetWindowTextW(g_hBtnAutoRefresh, L"Auto-refresh: PAUSE");
            }
            SaveSettings();
            break;

        case IDC_BTN_KILL:
            TerminateSelectedProcessImmediately();
            break;

        case IDC_BTN_HIDE_WINDOWS:
            g_bHideWindowsProcesses = !g_bHideWindowsProcesses;
            if (g_bHideWindowsProcesses) {
                SetWindowTextW(g_hBtnHideWindows, L"Processus Windows: MASQUÉS");
            } else {
                SetWindowTextW(g_hBtnHideWindows, L"Processus Windows: AFFICHÉS");
            }
            SaveSettings();
            RefreshProcessList();
            break;

        case IDC_BTN_EXCLUSIONS:
        case IDM_MANAGE_EXCLUSIONS: {
            HMENU hExclMenu = CreatePopupMenu();
            if (g_ExcludedCount == 0) {
                AppendMenuW(hExclMenu, MF_STRING | MF_GRAYED, 0, L"(Aucun processus exclu par son nom)");
            } else {
                AppendMenuW(hExclMenu, MF_STRING | MF_GRAYED, 0, L"--- Cliquer pour réafficher ---");
                for (int i = 0; i < g_ExcludedCount && i < 255; i++) {
                    WCHAR szMenuText[MAX_PATH + 32];
                    wsprintfW(szMenuText, L"Réafficher : %s", g_ExcludedProcesses[i]);
                    AppendMenuW(hExclMenu, MF_STRING, IDM_RESTORE_EXCLUSION_BASE + i, szMenuText);
                }
                AppendMenuW(hExclMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hExclMenu, MF_STRING, IDM_CLEAR_ALL_EXCLUSIONS, L"Tout réafficher (effacer les exclusions)");
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

                    AppendMenuW(hPopup, MF_STRING, IDM_KILL_IMMEDIATE, L"Tuer immédiatement\tSuppr");
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);

                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_REALTIME, L"Temps réel");
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_HIGH, L"Haute");
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_ABOVENORM, L"Supérieure à la normale");
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_NORMAL, L"Normale");
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_BELOWNORM, L"Inférieure à la normale");
                    AppendMenuW(hSubPriority, MF_STRING, IDM_PRIORITY_IDLE, L"Basse (Idle)");

                    AppendMenuW(hPopup, MF_POPUP, (UINT_PTR)hSubPriority, L"Définir la priorité");
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hPopup, MF_STRING, IDM_COPY_CMDLINE, L"Copier la ligne de commande (CLI)");
                    AppendMenuW(hPopup, MF_STRING, IDM_OPEN_LOCATION, L"Ouvrir l'emplacement du fichier");
                    AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);

                    // Option pour masquer ce processus par son nom
                    if (szProcName[0] != L'\0') {
                        WCHAR szHideText[MAX_PATH + 64];
                        wsprintfW(szHideText, L"Masquer \"%s\" par son nom", szProcName);
                        AppendMenuW(hPopup, MF_STRING, IDM_HIDE_BY_NAME, szHideText);
                    }
                    AppendMenuW(hPopup, MF_STRING, IDM_MANAGE_EXCLUSIONS, L"Gérer les processus exclus...");

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
        if (g_hBtnKill)        RemoveWindowSubclass(g_hBtnKill, ButtonSubclassProc, 12);
        if (g_hBtnHideWindows) RemoveWindowSubclass(g_hBtnHideWindows, ButtonSubclassProc, 13);
        if (g_hBtnExclusions)  RemoveWindowSubclass(g_hBtnExclusions, ButtonSubclassProc, 14);
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
        SaveSettings();
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
        lvc.pszText = (LPWSTR)s_BaseColumnTitles[i];
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
            wsprintfW(szTitle, L"%s %s", s_BaseColumnTitles[i], g_bSortAscending ? L"▲" : L"▼");
        } else {
            wcscpy_s(szTitle, sizeof(szTitle)/sizeof(WCHAR), s_BaseColumnTitles[i]);
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
        wcsncpy_s(pszCmdLine, maxChars, L"[Non disponible - NTAPI absente]", _TRUNCATE);
        return;
    }

    PROCESS_BASIC_INFORMATION pbi;
    ULONG len = 0;
    NTSTATUS status = g_pfnNtQueryInfoProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &len);
    if (status != 0 || !pbi.PebBaseAddress) {
        wcsncpy_s(pszCmdLine, maxChars, L"[Accès restreint]", _TRUNCATE);
        return;
    }

    PEB peb;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(peb), &bytesRead) || bytesRead == 0) {
        wcsncpy_s(pszCmdLine, maxChars, L"[Accès restreint PEB]", _TRUNCATE);
        return;
    }

    RTL_USER_PROCESS_PARAMETERS procParams;
    if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &procParams, sizeof(procParams), &bytesRead) || bytesRead == 0) {
        wcsncpy_s(pszCmdLine, maxChars, L"[Accès restreint Param]", _TRUNCATE);
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
        wcsncpy_s(pszCmdLine, maxChars, L"[Erreur lecture CLI]", _TRUNCATE);
    }
}

// Traduction de la classe de priorité en texte lisible
const WCHAR* PriorityClassToString(DWORD priorityClass) {
    switch (priorityClass) {
    case REALTIME_PRIORITY_CLASS:     return L"Temps réel";
    case HIGH_PRIORITY_CLASS:         return L"Haute";
    case ABOVE_NORMAL_PRIORITY_CLASS: return L"Supérieure";
    case NORMAL_PRIORITY_CLASS:       return L"Normale";
    case BELOW_NORMAL_PRIORITY_CLASS: return L"Inférieure";
    case IDLE_PRIORITY_CLASS:         return L"Basse";
    default:                          return L"Inconnue";
    }
}

// Mise à jour de la barre d'état avec métriques globales en temps réel
void UpdateStatusBar(int displayedCount, int totalCount, double totalCpu) {
    WCHAR szText[128];

    wsprintfW(szText, L"Processus : %d / %d", displayedCount, totalCount);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)szText);

    wsprintfW(szText, L"CPU Global : %.1f %%", totalCpu);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 1, (LPARAM)szText);

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        DWORD totalMB = (DWORD)(memStatus.ullTotalPhys / (1024 * 1024));
        DWORD usedMB  = (DWORD)((memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024 * 1024));
        wsprintfW(szText, L"RAM : %u / %u Mo (%u %%)", usedMB, totalMB, memStatus.dwMemoryLoad);
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 2, (LPARAM)szText);
    }

    wsprintfW(szText, L"Exclusions : %d actives | Tri : %s %s",
        g_ExcludedCount,
        s_BaseColumnTitles[g_SortColumn],
        g_bSortAscending ? L"▲" : L"▼"
    );
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 3, (LPARAM)szText);

    SendMessageW(g_hStatusBar, SB_SETTEXTW, 4, (LPARAM)L"Fermer: Réduire en Tray | Suppr: Tuer");

    if (g_bDarkModeActive && g_hStatusBar) {
        InvalidateRect(g_hStatusBar, NULL, FALSE);
    }
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

            // GPU estimé
            DWORD gdiObjects = GetGuiResources(hProcess, GR_GDIOBJECTS);
            DWORD userObjects = GetGuiResources(hProcess, GR_USEROBJECTS);
            if (gdiObjects > 0 || userObjects > 0) {
                proc->gpuPercent = (proc->cpuPercent * 0.45);
                if (proc->gpuPercent > 99.0) proc->gpuPercent = 99.0;
            }

            proc->priorityClass = GetPriorityClass(hProcess);
            GetProcessCommandLine(hProcess, proc->szCmdLine, sizeof(proc->szCmdLine) / sizeof(WCHAR));

            CloseHandle(hProcess);
        } else {
            wcsncpy_s(proc->szCmdLine, 1024, L"[Système / Accès restreint]", _TRUNCATE);
        }

        // Détection de processus Windows de base
        proc->isWindowsBase = IsWindowsBaseProcess(pid, proc->szName, proc->szFullPath);

        if (hist) {
            hist->timestamp = curTick;
        }
    } while (Process32NextW(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    PruneHistory(activePids, activeCount);

    // Calcul du GPU global et de la RAM pour l'icône Tray
    double totalGpuPercent = 0.0;
    for (int i = 0; i < g_ProcessCount; i++) {
        totalGpuPercent += g_Processes[i].gpuPercent;
    }
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

    // Mise à jour de l'historique 60s pour les graphiques CPU / RAM / GPU du bandeau
    if (g_PerfHistoryCount == 0) {
        for (int i = 0; i < PERF_HISTORY_POINTS; i++) {
            g_PerfCpuHistory[i] = totalCpuPercent;
            g_PerfRamHistory[i] = memPercent;
            g_PerfGpuHistory[i] = totalGpuPercent;
        }
        g_PerfHistoryCount = PERF_HISTORY_POINTS;
    } else {
        for (int i = 0; i < PERF_HISTORY_POINTS - 1; i++) {
            g_PerfCpuHistory[i] = g_PerfCpuHistory[i + 1];
            g_PerfRamHistory[i] = g_PerfRamHistory[i + 1];
            g_PerfGpuHistory[i] = g_PerfGpuHistory[i + 1];
        }
        g_PerfCpuHistory[PERF_HISTORY_POINTS - 1] = totalCpuPercent;
        g_PerfRamHistory[PERF_HISTORY_POINTS - 1] = memPercent;
        g_PerfGpuHistory[PERF_HISTORY_POINTS - 1] = totalGpuPercent;
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
        wsprintfW(szRam, L"%u Mo", (UINT)p->workingSetMB);
        swprintf_s(szDisk, sizeof(szDisk)/sizeof(WCHAR), L"%.2f Mo/s", p->diskMBPerSec);
        swprintf_s(szNet, sizeof(szNet)/sizeof(WCHAR), L"%.1f Ko/s", p->netKBPerSec);
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
