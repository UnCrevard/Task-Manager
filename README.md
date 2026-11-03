# Windows Task Manager en C Pur (Win32 API & MinGW-w64)

Ce projet est une implémentation native et légère d'un **Gestionnaire des Tâches Windows (Task Manager)** écrit en **C pur**, sans aucune bibliothèque externe tierce, utilisant uniquement l'API Windows de base (`Win32` & `NTAPI`).

Il est conçu pour être compilé directement avec **MinGW-w64** (`gcc`).

---

## 🚀 Fonctionnalités Implémentées

| Fonctionnalité | Implémentation Win32 |
| :--- | :--- |
| **Réduction en Tray (Fermeture)** | À la fermeture de la fenêtre (`WM_CLOSE`), l'application se réduit automatiquement dans la zone de notification Windows (System Tray) avec `ShowWindow(SW_HIDE)` sans interrompre la surveillance des ressources. |
| **3 Tray Icons distinctes (CPU, RAM, GPU)** | Au lieu d'une icône unique, l'application crée **3 icônes distinctes** dans la barre des tâches pour le **CPU**, la **RAM** et le **GPU** (`TRAY_ID_CPU`, `TRAY_ID_RAM`, `TRAY_ID_GPU`). |
| **Historique d'utilisation en temps réel (14s)** | Chaque icône génère en mémoire une image 16x16 32-bit ARGB (`CreateIconIndirect`) affichant un mini-graphique (sparkline en barres) des **14 dernières secondes** d'historique avec défilement continu et couleur dédiée : **CPU** (Vert émeraude / Alerte rouge si >80%), **RAM** (Violet vif / Alerte si >85%), **GPU** (Cyan électrique / Alerte si >80%). |
| **Restauration par double-clic** | Un double-clic gauche sur **n'importe laquelle des 3 icônes** restaure instantanément la fenêtre au premier plan (`ShowWindow(SW_RESTORE)` et `SetForegroundWindow`). Un clic droit ouvre un menu contextuel dédié affichant les détails de la ressource avec options pour ouvrir ou quitter définitivement (`WM_DESTROY`). |
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
| **Graphiques de performances 1 heure (3600s)** | Bandeau en haut de la fenêtre affichant l'historique complet sur **1 heure** (3600 échantillons à 1 seconde) pour le **CPU**, la **RAM** et le **GPU** avec repères temporels horizontaux (60m, 45m, 30m, 15m, 0s). |
| **Survol interactif & Processus responsable** | En passant la souris sur n'importe quel point du graphique d'historique 1h, un tracker vertical dynamique et une infobulle contrastée affichent le temps exact écoulé (`-XXm YYs`), la valeur de la ressource, et **le processus précis qui consommait le plus la ressource à ce moment-là** (Nom, PID, % ou Mo). |
| **Centralisation des textes dans `resource.rc`** | Tous les libellés de l'application (colonnes, boutons, menus contextuels, tray, barres d'état, tooltips) sont centralisés dans la table des chaînes (`STRINGTABLE`) de `resource.rc` via `resource.h` et chargés par `LoadStringW` (avec fallback mémoire garanti en cas de compilation directe). |
| **Suppression du bouton Tuer superflu** | Le bouton « Tuer » a été retiré de la barre d'outils au profit d'un agencement sans vide (la touche **Suppr** / `VK_DELETE` ou le clic droit permettant une terminaison immédiate sans confirmation). |
| **Terminaison brutale immédiate sans confirmation** | `OpenProcess(PROCESS_TERMINATE, FALSE, pid)` puis appel direct `TerminateProcess(hProcess, 1)` **sans aucune boîte de dialogue de confirmation**, déclenché par la touche **Suppr** (`VK_DELETE`) ou le menu contextuel. |
| **Instance unique (Single Instance)** | Empêche les lancements multiples simultanés via un Mutex nommé `Local\TaskManager_SingleInstance_Mutex_9D4F2` et restaure l'instance existante au premier plan. |
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

## 🎨 Icône Native de l'Application

L'application inclut une véritable icône dédiée de Task Manager (châssis sombre, quadrillage télémétrique, barre supérieure avec LEDs d'activité et onde ECG/impulsion CPU néon) :
- **Fichier `app.ico` multi-résolutions** : 16x16, 32x32 et 48x48 en 32-bit ARGB.
- **Intégration PE (`resource.rc`)** : Compilée par `windres` pour que l'exécutable `taskmgr.exe` arbore l'icône dans l'Explorateur Windows et sur le bureau.
- **Fallback programmatique automatique (`CreateTaskManagerAppIcon`)** : Si vous compilez directement `taskmgr.c` sans `windres`, le code C génère l'icône de zéro en mémoire GDI (`CreateIconIndirect`) et l'assigne à la fenêtre (`WM_SETICON`) !

---

## 🛠️ Compilation avec MinGW-w64

### Option 1 : Via `make` (avec intégration de l'icône via `windres`)

```cmd
mingw32-make
```
*(Cette commande compile automatiquement `resource.rc` avec `windres` puis lie `resource.o` à `taskmgr.exe`).*

### Option 2 : Compilation manuelle en 2 étapes (avec icône dans l'explorateur)

```cmd
windres resource.rc -O coff -o resource.o
gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c resource.o -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32
```

### Option 3 : Compilation directe ultra-simple (1 seule ligne)

```cmd
gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32
```
*(Le fallback automatique dans `taskmgr.c` générera l'icône en mémoire au lancement de l'application).*

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
