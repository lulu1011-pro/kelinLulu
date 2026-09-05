@echo off
echo ========================================
echo   MindVault Starting...
echo ========================================

echo [1/2] Starting backend...
start "" "D:\kelin\AI项目\mindvault-cpp\build\Release\mindvault.exe"

timeout /t 2 /nobreak >nul

echo [2/2] Starting frontend...
cd /d "D:\kelin\AI项目\mindvault-cpp\web"
start "" cmd /c "npm run dev"

timeout /t 3 /nobreak >nul

echo ========================================
echo   Started!
echo   Backend: http://127.0.0.1:8080
echo   Frontend: http://localhost:5173
echo ========================================

start http://localhost:5173

pause
