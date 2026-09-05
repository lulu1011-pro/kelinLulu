@echo off
echo ========================================
echo   MindVault Stopping...
echo ========================================

taskkill /F /IM mindvault.exe 2>nul
taskkill /F /IM node.exe 2>nul

echo ========================================
echo   Stopped!
echo ========================================

pause
