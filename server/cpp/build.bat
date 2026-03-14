@echo off
setlocal

cd /d "%~dp0"

echo ============================================
echo  VaultBox Desktop v0.5.0 - C++ Build
echo ============================================

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment.
    exit /b 1
)

echo.
echo [1/2] Compiling SQLite...
cl /c /O2 /DSQLITE_THREADSAFE=1 /DSQLITE_ENABLE_FTS5 /DSQLITE_ENABLE_JSON1 deps\sqlite3.c /Fo:sqlite3.obj /nologo
if errorlevel 1 (
    echo ERROR: SQLite compilation failed.
    exit /b 1
)

echo.
echo [2/2] Compiling VaultBox Desktop (WebView2)...
cl /EHsc /O2 /std:c++17 /bigobj /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"deps\webview2" vaultbox_server.cpp sqlite3.obj deps\webview2\WebView2LoaderStatic.lib /Fe:VaultBox-Server.exe /nologo /link ws2_32.lib bcrypt.lib rpcrt4.lib shell32.lib user32.lib gdi32.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib comdlg32.lib ole32.lib
if errorlevel 1 (
    echo ERROR: Compilation failed.
    exit /b 1
)

echo.
echo ============================================
echo  Build complete: VaultBox-Server.exe
echo ============================================
dir VaultBox-Server.exe

endlocal
