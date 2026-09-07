@echo off
echo ========================================
echo   MindVault Restarter
echo ========================================

echo [1/3] Stopping old processes ...
taskkill /F /IM mindvault.exe 2>nul
taskkill /F /IM node.exe 2>nul
timeout /t 1 /nobreak >nul

echo [2/3] Starting backend ...
start "MindVault-Backend" "D:\kelin\AI项目\mindvault-cpp\build\Release\mindvault.exe"

echo [3/3] Starting frontend ...
cd /d "D:\kelin\AI项目\mindvault-cpp\web"
start "MindVault-Frontend" cmd /k "npm run dev"

echo.
echo Waiting for frontend port 5173 to be ready ...

set tries=0
:wait_5173
timeout /t 1 /nobreak >nul
set /a tries+=1
netstat -ano 2>nul | findstr /c:":5173" | findstr /i "listening" >nul
if not errorlevel 1 goto front_ready
if %tries% geq 45 goto front_timeout
goto wait_5173

:front_timeout
echo [WARN] Frontend not ready after 45 seconds.
echo        Open the "MindVault-Frontend" window to see the error message.
goto show_info

:front_ready
echo Frontend is ready. Opening browser...
start http://localhost:5173

:show_info
echo ========================================
echo   Backend : http://127.0.0.1:8080
echo   Frontend: http://localhost:5173
echo   Keep the two new windows open.
echo   Closing MindVault-Backend or MindVault-Frontend stops the app.
echo ========================================
pause
