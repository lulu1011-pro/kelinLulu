@echo off
echo ========================================
echo   MindVault Building...
echo ========================================

echo [1/2] Stopping old process...
taskkill /F /IM mindvault.exe 2>nul
timeout /t 1 /nobreak >nul

echo [2/2] Building C++ backend...
cd /d "D:\kelin\AI项目\mindvault-cpp\build"
"D:\VC2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build . --config Release

if %errorlevel%==0 (
    echo ========================================
    echo   Build Success!
    echo ========================================
) else (
    echo ========================================
    echo   Build Failed! Check errors above.
    echo ========================================
)

pause
