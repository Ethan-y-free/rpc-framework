@echo off
rem start rpc-framework in project directory
cd /d "%~dp0"
claude
if errorlevel 1 pause
