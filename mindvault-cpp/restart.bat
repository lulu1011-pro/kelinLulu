@echo off
echo ========================================
echo   MindVault Restarting...
echo ========================================

echo [1/3] Stopping...
taskkill /F /IM mindvault.exe 2>nul
taskkill /F /IM node.exe 2>nul
timeout /t 1 /nobreak >nul

echo [2/3] Starting backend...
start "" "D:\kelin\AI项目\mindvault-cpp\build\Release\mindvault.exe"
timeout /t 2 /nobreak >nul

echo [3/3] Starting frontend...
cd /d "D:\kelin\AI项目\mindvault-cpp\web"
start "" cmd /c "npm run dev"
timeout /t 3 /nobreak >nul

echo ========================================
echo   Restarted!
echo   Backend: http://127.0.0.1:8080
echo   Frontend: http://localhost:5173
echo ========================================

start http://localhost:5173

pause
