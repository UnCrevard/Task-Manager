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
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnAutoRefresh = NULL;
static HWND g_hBtnKill = NULL;
static HWND g_hBtnHideWindows = NULL;
static HWND g_hBtnExclusions = NULL;
static HWND g_hEditSearch = NULL;
static WCHAR g_szSearchFilter[128] = { 0 };
static HFONT g_hFont = NULL;

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
// Gestion du thème visuel moderne Windows (suppression de l'apparence 98)
// ============================================================================

// Détection du mode sombre Windows (Windows 10 build 17763+ et Windows 11)
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

// Application des attributs DWM modernes (Barre de titre sombre immersive sous Windows 10/11 si activée)
static void ApplyDwmModernAttributes(HWND hWnd) {
    if (!hWnd) return;
    HMODULE hDwmapi = LoadLibraryW(L"dwmapi.dll");
    if (hDwmapi) {
        typedef HRESULT (WINAPI *pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfnDwmSetWindowAttribute pDwmSet = (pfnDwmSetWindowAttribute)GetProcAddress(hDwmapi, "DwmSetWindowAttribute");
        if (pDwmSet) {
            BOOL bDarkMode = IsWindowsDarkModeActive();
            // DWMWA_USE_IMMERSIVE_DARK_MODE (20 pour Windows 11 et Win 10 2004+, 19 pour Win 10 1809)
            pDwmSet(hWnd, 20, &bDarkMode, sizeof(bDarkMode));
            pDwmSet(hWnd, 19, &bDarkMode, sizeof(bDarkMode));
        }
        FreeLibrary(hDwmapi);
    }
}

// Application du thème moderne officiel Windows (Explorer style au lieu du style 98)
static void ApplyModernWindowsTheme(HWND hWnd, HWND hListView) {
    HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme) {
        typedef HRESULT (WINAPI *pfnSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
        pfnSetWindowTheme pSetWindowTheme = (pfnSetWindowTheme)GetProcAddress(hUxTheme, "SetWindowTheme");
        if (pSetWindowTheme && hListView) {
            BOOL bDarkMode = IsWindowsDarkModeActive();
            // Utiliser le thème Explorer officiel de Windows (rendu moderne avec survol et sélection bleutée translucide)
            if (bDarkMode) {
                pSetWindowTheme(hListView, L"DarkMode_Explorer", NULL);
            } else {
                pSetWindowTheme(hListView, L"Explorer", NULL);
            }
            HWND hHeader = ListView_GetHeader(hListView);
            if (hHeader) {
                if (bDarkMode) {
                    pSetWindowTheme(hHeader, L"DarkMode_ItemsView", NULL);
                } else {
                    pSetWindowTheme(hHeader, L"Explorer", NULL);
                }
            }
        }
        FreeLibrary(hUxTheme);
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

    const WCHAR CLASS_NAME[] = L"Win32NativeTaskManager";
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_hAppIcon ? g_hAppIcon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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

        // 7. Contrôle ListView SysListView32 avec bordure native subtile
        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            10, 48, 1220, 600,
            hWnd, (HMENU)IDC_LISTVIEW, GetModuleHandle(NULL), NULL
        );
        SendMessage(g_hListView, WM_SETFONT, (WPARAM)hFont, TRUE);

        ListView_SetExtendedListViewStyle(
            g_hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER
        );

        InitListViewColumns(g_hListView);

        // Application du thème visuel moderne Explorer et police sur l'en-tête
        ApplyModernWindowsTheme(hWnd, g_hListView);
        HWND hHeader = ListView_GetHeader(g_hListView);
        if (hHeader) {
            SendMessage(hHeader, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

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

        return 0;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        SendMessage(g_hStatusBar, WM_SIZE, 0, 0);
        RECT rcStatus;
        GetWindowRect(g_hStatusBar, &rcStatus);
        int statusHeight = rcStatus.bottom - rcStatus.top;

        int listTop = 48;
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

    case WM_CTLCOLORSTATIC: {
        // Fond transparent pour le label 'Filtrer :' afin qu'il s'intègre au fond natif Windows
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        // Réaction dynamique si l'utilisateur change de thème ou de mode sombre Windows
        ApplyModernWindowsTheme(hWnd, g_hListView);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    case WM_DESTROY:
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
