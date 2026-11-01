# Windows Win32 Task Manager en C Pur (MinGW-w64)

Développement direct et exclusif de l'application native Windows en C pur (`taskmgr.c`), compilable sans aucune dépendance tierce à l'aide de MinGW-w64 (`gcc`), avec interface graphique native Win32 (`SysListView32`), rafraîchissement automatique à 1 seconde, extraction des arguments CLI, statistiques en temps réel, changement de priorité et terminaison immédiate sans confirmation.

---

### User Review & Critical Decisions

> [!IMPORTANT]
> **Ajustement validé selon votre demande :**
> - **Focus exclusif sur le code C** : Tout le projet est centré sur la production des fichiers sources C natifs Windows (`taskmgr.c`, `Makefile`, `build.bat`, `README.md`).
> - **Compilateur ciblé** : `x86_64-w64-mingw32-gcc` / `gcc` standard sous MinGW-w64.
> - **Zéro dépendance externe** : Utilisation stricte des bibliothèques systèmes fournies en standard avec Windows (`-mwindows -lcomctl32 -lpsapi -lntdll -lole32`).
> - **GUI Win32 native** : Fenêtre Win32 native créée avec `CreateWindowEx`, contrôle de liste `SysListView32` en vue rapport (tableau plat sans tree view), double-buffering anti-scintillement, menus contextuels et barre d'état.
> - **Terminaison brutale immédiate** : `TerminateProcess(hProcess, 1)` exécuté instantanément lors de l'appui sur `Suppr` ou via le bouton/menu, sans aucune boîte de dialogue bloquante (`MessageBox`).
> - **Rafraîchissement automatique (1s)** : Timer Win32 (`SetTimer` à 1000 ms) avec bouton à bascule ON/OFF dans la barre d'outils et menu pour activer/désactiver le rafraîchissement automatique.
> - **Métriques complètes par processus** :
>   * PID et Nom de l'exécutable
>   * CPU % (delta en temps réel sur 1 seconde via `GetProcessTimes` et `GetSystemTimes`)
>   * Mémoire (Mo) via `GetProcessMemoryInfo` (`WorkingSetSize`)
>   * I/O Disque (Mo/s) calculé via le delta de `GetProcessIoCounters`
>   * I/O Réseau estimé/suivi via compteurs I/O
>   * Utilisation GPU (comptabilisation d'activité graphique ou statut)
>   * Priorité (Idle, Below Normal, Normal, Above Normal, High, Realtime) avec possibilité de modification directe via `SetPriorityClass`
>   * Ligne de commande complète (**CLI args**) extraite depuis la mémoire du processus via `NtQueryInformationProcess` et `ReadProcessMemory` sur le PEB (`RTL_USER_PROCESS_PARAMETERS`).

---

## 1. Vue d'Ensemble & Spécifications du Code C

Le fichier principal `taskmgr.c` sera un code C autonome, propre, modulaire et structuré, intégrant l'ensemble des fonctionnalités demandées :

```
┌────────────────────────────────────────────────────────────────────────┐
│                        taskmgr.c (MinGW-w64 C)                         │
├────────────────────────────────────────────────────────────────────────┤
│ 1. WinMain & Window Procedure (WndProc)                                │
│    - Initialisation des Common Controls (InitCommonControlsEx)         │
│    - Création de la fenêtre principale (CreateWindowExW)               │
│    - Contrôle SysListView32 en mode LVS_REPORT                         │
│    - Barre d'outils & boutons : [Auto-refresh 1s : ON/OFF],            │
│      [Actualiser], [Tuer Immédiatement (Suppr)], [Priorité]            │
│    - Barre d'état (StatusBar) affichant CPU global, RAM et processus   │
│                                                                        │
│ 2. Moteur de Collecte Système & Métriques                              │
│    - CreateToolhelp32Snapshot / Process32First / Process32Next         │
│    - Snapshot précédent vs actuel pour calcul des deltas (CPU %, Disk) │
│    - GetProcessMemoryInfo (WorkingSetSize en Mo)                       │
│    - GetProcessIoCounters (Read/Write bytes / sec)                     │
│    - NtQueryInformationProcess (Résolution de la ligne de commande CLI)│
│                                                                        │
│ 3. Actions Processus                                                   │
│    - Terminaison immédiate : OpenProcess + TerminateProcess(h, 1)      │
│    - Changement de priorité : OpenProcess + SetPriorityClass(h, class) │
│                                                                        │
│ 4. Timer & Rafraîchissement                                            │
│    - SetTimer(hWnd, ID_TIMER_REFRESH, 1000, NULL)                      │
│    - Mise à jour différentielle de la ListView (évite le scintillement)│
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Extraction Technique de la Ligne de Commande (CLI Args)

Sous Windows, la ligne de commande complète d'un processus arbitraire n'est pas exposée par `psapi.h` classique. Elle se trouve dans le **Process Environment Block (PEB)**. L'implémentation C utilisera l'approche propre et éprouvée :
1. Appel dynamique ou liaison à `NtQueryInformationProcess` (`ntdll.dll`).
2. Récupération de la structure `PROCESS_BASIC_INFORMATION` pour obtenir l'adresse `PebBaseAddress`.
3. Lecture de la structure `PEB` via `ReadProcessMemory` pour localiser le pointeur `ProcessParameters` (`RTL_USER_PROCESS_PARAMETERS`).
4. Lecture de `CommandLine` (`UNICODE_STRING`) contenant le buffer et la taille en octets.
5. Conversion UTF-16 vers ANSI/UTF-8 pour affichage dans la ListView.

---

## 3. Fichiers Livrés dans le Dépôt

1. **`taskmgr.c`** : Code source C complet, documenté avec commentaires clairs, sans dépendance tierce.
2. **`Makefile`** : Fichier Makefile prêt à l'emploi pour compiler sous Windows avec MinGW (`make` ou `mingw32-make`).
3. **`build.bat`** : Script batch Windows en un clic permettant de compiler avec `gcc` et de lancer `taskmgr.exe`.
4. **`README.md`** : Guide complet d'installation de MinGW-w64, commandes de compilation (32-bit et 64-bit), et explications détaillées des privilèges requis (`SeDebugPrivilege`).
5. **Interface Web d'accompagnement** : Écran épuré permettant de visualiser le code C avec coloration, de prévisualiser l'architecture et de télécharger directement les fichiers sources et archives en un clic.

---

## 4. Commande de Compilation MinGW-w64

```bash
gcc -mwindows -O2 -Wall -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll -lole32
```
*Options :*
- `-mwindows` : Supprime la fenêtre de console noire pour exécuter directement l'application GUI.
- `-O2` : Optimisation maximale pour minimiser l'empreinte CPU et mémoire de l'outil.
- `-lcomctl32` : Bibliothèque des Common Controls Windows (`SysListView32`, `InitCommonControlsEx`).
- `-lpsapi` : Process Status API (`GetProcessMemoryInfo`).
- `-lntdll` : Fonctions NT natives (`NtQueryInformationProcess`).
- `-lole32` : Initialisation COM pour composants additionnels si nécessaire.
