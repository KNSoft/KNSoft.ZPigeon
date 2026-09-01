@echo off
curl.exe --fail --location "https://models.dev/api.json" --output "%~dp0api.json.tmp" || exit /b 1
move /y "%~dp0api.json.tmp" "%~dp0api.json" >nul
