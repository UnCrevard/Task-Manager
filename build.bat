@echo off
REM ============================================================================
REM Script de compilation MinGW-w64 pour le Task Manager Windows en C
REM ============================================================================

echo [1/3] Verification du compilateur gcc (MinGW-w64)...
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] GCC n'est pas present dans votre variable PATH.
    echo Veuillez installer MinGW-w64 (via MSYS2, WinLibs ou w64devkit) et l'ajouter a votre PATH.
    pause
    exit /b 1
)

echo [2/3] Compilation des ressources (icone app.ico)...
where windres >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    windres resource.rc -O coff -o resource.res
    set RES_OBJ=resource.res
) else (
    echo Note: windres introuvable, utilisation de l'icone programmee en code C.
    set RES_OBJ=
)

echo [3/3] Compilation de taskmgr.c...
gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c %RES_OBJ% -lcomctl32 -lpsapi -lntdll -lole32 -lshell32 -lgdi32

if %ERRORLEVEL% EQU 0 (
    echo =========================================================
    echo SUCCES : taskmgr.exe a ete genere avec succes !
    echo Lancement du Task Manager...
    echo =========================================================
    start taskmgr.exe
) else (
    echo [ERREUR] La compilation a echoue. Verifiez les messages ci-dessus.
    pause
)
