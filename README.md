# Windows Task Manager en C Pur (Win32 API & MinGW-w64)

Ce projet est une implémentation native et légère d'un **Gestionnaire des Tâches Windows (Task Manager)** écrit en **C pur**, sans aucune bibliothèque externe tierce, utilisant uniquement l'API Windows de base (`Win32` & `NTAPI`).

Il est conçu pour être compilé directement avec **MinGW-w64** (`gcc`).

---

## 🚀 Fonctionnalités Implémentées

| Fonctionnalité | Implémentation Win32 |
| :--- | :--- |
| **Réduction en Tray Icon (Fermeture)** | À la fermeture de la fenêtre (`WM_CLOSE`), l'application se réduit automatiquement dans la zone de notification Windows (System Tray) avec `ShowWindow(SW_HIDE)` sans interrompre la surveillance des ressources. |
| **Icône Tray dynamique à 3 couleurs** | Génération en mémoire d'une icône 16x16 32-bit ARGB (`CreateIconIndirect`) affichant 3 barres de jauge en temps réel : **CPU** (Vert émeraude / Rouge si >80%), **RAM** (Violet / Rouge si >85%), **GPU** (Cyan / Rouge si >80%). Mise à jour chaque seconde sans fuite de handles GDI. |
| **Restauration par double-clic** | Un double-clic gauche sur l'icône tray restaure instantanément la fenêtre au premier plan (`ShowWindow(SW_RESTORE)` et `SetForegroundWindow`). Un clic droit ouvre un menu contextuel pour ouvrir ou quitter définitivement l'application (`WM_DESTROY`). |
| **Correction d'affichage des en-têtes** | Les noms de colonnes restent **toujours visibles** lors du tri via l'API officielle `ListView_SetColumn` (au lieu de manipuler directement le masque de texte du header qui provoquait la disparition des titres). |
| **Tri interactif avec indicateur (`▲` / `▼`)** | Clic sur n'importe quel en-tête (`LVN_COLUMNCLICK`) pour trier en direct (croissant/décroissant) avec affichage du symbole textuel et de la flèche visuelle standard (`HDF_SORTUP` / `HDF_SORTDOWN`) via `qsort`. |
| **Masquer un processus par son nom** | Clic droit sur un processus -> *« Masquer <nom> par son nom »*. Le processus est exclu de l'affichage et ajouté à la liste d'exclusions. |
| **Gestionnaire d'exclusions** | Bouton `Exclusions (N)` dans la barre d'outils ouvrant un menu pour réafficher individuellement un processus masqué ou réinitialiser toutes les exclusions. |
| **Persistance `settings.ini`** | Enregistre et recharge automatiquement tous les réglages et les exclusions dans `settings.ini` dans le dossier de l'exécutable via `GetPrivateProfileStringW` / `WritePrivateProfileStringW`. |
| **Masquer les processus Windows de base** | Bouton de bascule `Processus Windows : MASQUÉS / AFFICHÉS` détectant les processus système (`svchost.exe`, `csrss.exe`, `explorer.exe`, etc.), l'arborescence `%SystemRoot%` et la signature constructeur Microsoft. |
| **Moteur anti-scintillement (Zéro clignotement)** | Mise à jour différentielle sélective en place des cellules sans jamais détruire la liste (`ListView_DeleteAllItems` banni), `LVS_EX_DOUBLEBUFFER` et conservation de la sélection. |
| **Tableau plat des processus (Non-Tree)** | Contrôle natif `SysListView32` avec style `LVS_REPORT`, `LVS_EX_FULLROWSELECT`, `LVS_EX_GRIDLINES` et `LVS_EX_DOUBLEBUFFER`. |
| **Arguments de ligne de commande (CLI Args)** | Lecture du PEB (`PebBaseAddress`) via `NtQueryInformationProcess` (`ntdll.dll`) et `ReadProcessMemory` sur `RTL_USER_PROCESS_PARAMETERS.CommandLine`. |
| **CPU % en temps réel** | Calcul différentiel à chaque intervalle via `GetProcessTimes` (Kernel + User) et `GetSystemTimes`. |
| **Mémoire vive (RAM)** | `GetProcessMemoryInfo` (structure `PROCESS_MEMORY_COUNTERS_EX`, champ `WorkingSetSize` en Mo). |
| **I/O Disque (Mo/s)** | `GetProcessIoCounters` : delta de `ReadTransferCount` + `WriteTransferCount` divisé par l'intervalle de temps. |
| **I/O Réseau (Ko/s)** | `GetProcessIoCounters` : delta de `OtherTransferCount` (flux sockets / pipes / I/O réseau). |
| **Estimation GPU %** | Estimation basée sur l'activité des contextes et objets graphiques `GetGuiResources` (`GR_GDIOBJECTS`, `GR_USEROBJECTS`). |
| **Changement de priorité** | `GetPriorityClass` et `SetPriorityClass` (Temps réel, Haute, Supérieure, Normale, Inférieure, Basse) via clic droit ou menu. |
| **Terminaison brutale immédiate** | `OpenProcess(PROCESS_TERMINATE, FALSE, pid)` puis appel direct `TerminateProcess(hProcess, 1)` **sans aucune boîte de dialogue de confirmation**. Déclenché par le bouton ou la touche **Suppr** (`VK_DELETE`). |
| **Rafraîchissement automatique 1s** | Timer Windows `SetTimer(hWnd, IDT_REFRESH_TIMER, 1000, NULL)` avec bouton bascule d'état (ON/OFF). |
| **Statistiques en temps réel** | Barre d'état `StatusBar` affichant en temps réel le nombre de processus, le CPU global %, et la RAM physique occupée / totale via `GlobalMemoryStatusEx`. |
| **Privilèges élevés** | Activation automatique de `SeDebugPrivilege` pour inspecter tous les processus accessibles du système. |

---

## ⚙️ Fichier de configuration `settings.ini`

Le programme crée ou met à jour automatiquement `settings.ini` dans le dossier de l'exécutable :

```ini
[Settings]
AutoRefresh=1
HideWindowsProcesses=0
SortColumn=2
SortAscending=0

[Exclusions]
Count=2
Process0=discord.exe
Process1=spotify.exe
```

---

## 🛠️ Compilation avec MinGW-w64

### 1. Commande directe dans le terminal (CMD ou PowerShell)

```cmd
gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32
```

### 2. Ou via le script fourni `build.bat`

Double-cliquez simplement sur `build.bat` sous Windows pour compiler et lancer l'application.

### 3. Ou via `make`

```cmd
mingw32-make
```

---

## ⌨️ Raccourcis Clavier & Contrôles

- **Suppr (`Delete`)** : Tuer immédiatement le processus sélectionné (sans confirmation).
- **F5** : Forcer un rafraîchissement immédiat de la liste et des métriques.
- **Clic-droit** sur une ligne :
  - *Tuer immédiatement* (`Suppr`)
  - *Définir la priorité* (Temps réel, Haute, Supérieure, Normale, Inférieure, Basse)
  - *Copier la ligne de commande (CLI)* dans le presse-papier
  - *Ouvrir l'emplacement du fichier* dans l'Explorateur Windows
  - *Masquer ce processus par son nom* (ajoute le nom à la liste d'exclusions persistante)
- **Bouton « Exclusions (N) »** : Affiche la liste des processus masqués pour les réafficher individuellement ou tout restaurer.
- **Bouton « Processus Windows »** : Masque/affiche les processus natifs de l'OS.
- **Bouton « Auto-refresh »** : Active ou suspend le timer de rafraîchissement à 1 seconde.
- **Fermeture de la fenêtre (`[X]`)** : Réduit automatiquement l'application dans la barre des tâches / System Tray.
- **Double-clic sur l'icône Tray** : Réaffiche et restaure l'application au premier plan.
- **Clic-droit sur l'icône Tray** : Ouvre le menu contextuel (« Ouvrir le Task Manager » ou « Quitter définitivement »).
