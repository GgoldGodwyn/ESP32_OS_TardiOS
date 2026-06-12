@echo off
set "IDF_PATH=C:\esp\v6.0.1\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif"
set "PATH=C:\Espressif\tools\python\v6.0.1\venv\Scripts;%PATH%"
cd /D "C:\Users\LENOVO\Documents\PlatformIO\Projects\pocketos-phase1"

echo --- Building storage image ---
python make_storage.py
if errorlevel 1 goto :fail

echo --- Flashing storage partition ---
python -m esptool --port %1 --baud 921600 write_flash 0x310000 storage.bin
if errorlevel 1 goto :fail

echo Done.
goto :end

:fail
echo FAILED
exit /b 1

:end
