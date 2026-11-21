@echo off
rem ===========================================================================
rem  KlankModule - Windows x64 build
rem
rem  Requirements (one time):
rem    - Visual Studio 2022 (Community is free) with "Desktop development with C++"
rem    - CMake 3.22+ (included with Visual Studio) and Git
rem  Optional:
rem    - Steinberg ASIO SDK unzipped to ThirdParty\asiosdk  (enables ASIO)
rem
rem  Usage:  Scripts\build_windows.bat            (Release build + tests)
rem          Scripts\build_windows.bat nosfz      (without the SFZ engine)
rem ===========================================================================
setlocal
cd /d "%~dp0\.."

where cmake >nul 2>nul
if errorlevel 1 (
    echo [!] CMake niet gevonden. Open de "x64 Native Tools Command Prompt for VS 2022" en probeer opnieuw.
    exit /b 1
)

set EXTRA=
if exist "ThirdParty\asiosdk\common\iasiodrv.h" (
    echo [i] ASIO SDK gevonden - ASIO wordt ingebouwd.
    set EXTRA=-DKM_ASIO_SDK_DIR="%CD%\ThirdParty\asiosdk"
) else (
    echo [i] Geen ASIO SDK in ThirdParty\asiosdk - build met WASAPI. Zie README voor ASIO.
)
if /i "%1"=="nosfz" set EXTRA=%EXTRA% -DKM_WITH_SFIZZ=OFF

echo.
echo === Configureren (de eerste keer worden JUCE, sfizz en Catch2 gedownload) ===
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 %EXTRA%
if errorlevel 1 exit /b 1

echo.
echo === Bouwen (Release) ===
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo === Tests ===
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 (
    echo [!] Er zijn tests mislukt - zie hierboven.
    exit /b 1
)

echo.
echo Klaar! Start de app:
echo   build\KlankModule_artefacts\Release\KlankModule.exe
endlocal
