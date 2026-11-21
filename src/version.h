// ============================================================================
// version.h - Version de l'application (incrémentée à chaque changement de code)
// ============================================================================

#ifndef VERSION_H
#define VERSION_H

#define APP_VER_MAJOR 1
#define APP_VER_MINOR 2
#define APP_VER_PATCH 0
#define APP_VER_BUILD 6

#define APP_STR_HELPER(x) #x
#define APP_STR(x) APP_STR_HELPER(x)

#define APP_WSTR_HELPER(x) L##x
#define APP_WSTR(x) APP_WSTR_HELPER(x)

#define APP_VERSION_SHORT_A \
    APP_STR(APP_VER_MAJOR) "." APP_STR(APP_VER_MINOR) "." APP_STR(APP_VER_PATCH)

#define APP_VERSION_FULL_A \
    APP_STR(APP_VER_MAJOR) "." APP_STR(APP_VER_MINOR) "." APP_STR(APP_VER_PATCH) "." APP_STR(APP_VER_BUILD)

#define APP_VERSION_SHORT_W \
    APP_WSTR(APP_STR(APP_VER_MAJOR)) L"." APP_WSTR(APP_STR(APP_VER_MINOR)) L"." APP_WSTR(APP_STR(APP_VER_PATCH))

#define APP_VERSION_FULL_W \
    APP_WSTR(APP_STR(APP_VER_MAJOR)) L"." APP_WSTR(APP_STR(APP_VER_MINOR)) L"." APP_WSTR(APP_STR(APP_VER_PATCH)) L"." APP_WSTR(APP_STR(APP_VER_BUILD))

#endif // VERSION_H
