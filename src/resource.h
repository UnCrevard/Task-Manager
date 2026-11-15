// ============================================================================
// resource.h - Identifiants des ressources du Gestionnaire des tâches Win32
// ============================================================================

#ifndef RESOURCE_H
#define RESOURCE_H

#define IDI_APP_ICON                    1
#ifndef CREATEPROCESS_MANIFEST_RESOURCE_ID
#define CREATEPROCESS_MANIFEST_RESOURCE_ID 1
#endif
#ifndef RT_MANIFEST
#define RT_MANIFEST                     24
#endif

// Décalage pour les chaînes anglaises dans la STRINGTABLE (FR: 5000+, EN: 6000+)
#define IDS_EN_OFFSET                   1000
#define EN_ID(id)                       ((id) + IDS_EN_OFFSET)

// --- Identifiants de la table des chaînes (STRINGTABLE) ---

// Titre de l'application
#define IDS_APP_TITLE                   5001

// Colonnes de la ListView
#define IDS_COL_NAME                    5010
#define IDS_COL_PID                     5011
#define IDS_COL_CPU                     5012
#define IDS_COL_RAM                     5013
#define IDS_COL_DISK                    5014
#define IDS_COL_NET                     5015
#define IDS_COL_GPU                     5016
#define IDS_COL_PRIORITY                5017
#define IDS_COL_CMDLINE                 5018

// Boutons et contrôles de la barre d'outils
#define IDS_BTN_AUTOREFRESH_ON          5020
#define IDS_BTN_AUTOREFRESH_OFF         5021
#define IDS_BTN_REFRESH                 5022
#define IDS_BTN_WINDOWS_ON              5023
#define IDS_BTN_WINDOWS_OFF             5024
#define IDS_BTN_EXCLUSIONS_FMT          5025
#define IDS_LABEL_FILTER                5026
#define IDS_BTN_SETTINGS                5027
#define IDS_BTN_ABOUT                   5028

// Menu contextuel
#define IDS_MENU_KILL_IMMEDIATE         5030
#define IDS_MENU_PRIORITY_REALTIME      5031
#define IDS_MENU_PRIORITY_HIGH          5032
#define IDS_MENU_PRIORITY_ABOVENORM     5033
#define IDS_MENU_PRIORITY_NORMAL        5034
#define IDS_MENU_PRIORITY_BELOWNORM     5035
#define IDS_MENU_PRIORITY_IDLE          5036
#define IDS_MENU_SET_PRIORITY           5037
#define IDS_MENU_COPY_CMDLINE           5038
#define IDS_MENU_OPEN_LOCATION          5039
#define IDS_MENU_HIDE_BY_NAME_FMT       5040
#define IDS_MENU_MANAGE_EXCLUSIONS      5041
#define IDS_MENU_NO_EXCLUSIONS          5042
#define IDS_MENU_RESTORE_HEADER         5043
#define IDS_MENU_RESTORE_ITEM_FMT       5044
#define IDS_MENU_CLEAR_EXCLUSIONS       5045

// Zone de notification (System Tray)
#define IDS_TRAY_OPEN                   5050
#define IDS_TRAY_EXIT                   5051
#define IDS_TRAY_CPU_FMT                5052
#define IDS_TRAY_RAM_FMT                5053
#define IDS_TRAY_GPU_FMT                5054
#define IDS_TRAY_TIP_RAM_FMT            5055

// Barre d'état (Status Bar)
#define IDS_STATUS_PROCESSES_FMT        5060
#define IDS_STATUS_CPU_FMT              5061
#define IDS_STATUS_RAM_FMT              5062
#define IDS_STATUS_EXCLUSIONS_FMT       5063

// Graphiques de performances 1 heure
#define IDS_GRAPH_CPU_TITLE             5070
#define IDS_GRAPH_CPU_SUB               5071
#define IDS_GRAPH_RAM_TITLE             5072
#define IDS_GRAPH_RAM_SUB_FMT           5073
#define IDS_GRAPH_RAM_SUB_DEFAULT       5074
#define IDS_GRAPH_GPU_TITLE             5075
#define IDS_GRAPH_GPU_SUB               5076

// Infobulles / Tooltips au survol de l'historique 1h
#define IDS_TIP_TIME_MIN_SEC            5080
#define IDS_TIP_TIME_SEC                5081
#define IDS_TIP_TOP_PROC                5082
#define IDS_TIP_USAGE_PCT               5083
#define IDS_TIP_USAGE_RAM               5084
#define IDS_TIP_NO_DATA                 5085

// Classes de priorité (affichage liste)
#define IDS_PRIO_REALTIME               5090
#define IDS_PRIO_HIGH                   5091
#define IDS_PRIO_ABOVENORM              5092
#define IDS_PRIO_NORMAL                 5093
#define IDS_PRIO_BELOWNORM              5094
#define IDS_PRIO_IDLE                   5095
#define IDS_PRIO_UNKNOWN                5096

// Ligne de commande restreinte & unités
#define IDS_STR_RESTRICTED              5100
#define IDS_STR_RESTRICTED_ACCESS       5101
#define IDS_STR_NO_NTAPI                5102
#define IDS_UNIT_RAM_FMT                5103
#define IDS_UNIT_DISK_FMT               5104
#define IDS_UNIT_NET_FMT                5105

// Fenêtre Paramètres (Settings)
#define IDS_SETTINGS_TITLE              5110
#define IDS_SETTINGS_LANG_LABEL         5111
#define IDS_SETTINGS_LANG_AUTO          5112
#define IDS_SETTINGS_LANG_EN            5113
#define IDS_SETTINGS_LANG_FR            5114
#define IDS_SETTINGS_AUTOREFRESH        5115
#define IDS_SETTINGS_HIDE_WINDOWS       5116
#define IDS_SETTINGS_ACTIVE_LANG_FMT    5117
#define IDS_BTN_OK                      5118
#define IDS_BTN_CANCEL                  5119

// Fenêtre À propos (About)
#define IDS_ABOUT_TITLE                 5130
#define IDS_ABOUT_APP_NAME              5131
#define IDS_ABOUT_VERSION_FMT           5132
#define IDS_ABOUT_COMPILE_FMT           5133
#define IDS_ABOUT_TECH                  5134
#define IDS_ABOUT_DESC                  5135
#define IDS_BTN_CLOSE                   5136

#endif // RESOURCE_H
