@echo off
set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.1"
set "IDF_TOOLS_PATH=C:\Espressif"
set "PATH=C:\Espressif\tools\cmake\3.24.0\bin;C:\Espressif\tools\ninja\1.11.1;C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;%PATH%"

if not "%1"=="" (
    set "COM_PORT=%1"
    goto :flash
)

echo --- Auto-detecting ESP32 COM port ---
for /f "usebackq tokens=*" %%i in (`powershell -NoProfile -Command "Get-WMIObject Win32_SerialPort | Where-Object {$_.Description -match 'CP210|CH340|CH343|UART|USB Serial|CDC'} | Select-Object -First 1 -ExpandProperty DeviceID"`) do set "COM_PORT=%%i"

if "%COM_PORT%"=="" (
    echo ERROR: No ESP32 COM port found. Plug in the board or pass the port manually:
    echo   .\flash_fw.bat COM14
    echo.
    echo Available ports:
    powershell -NoProfile -Command "Get-WMIObject Win32_SerialPort | Select-Object DeviceID, Description | Format-Table -AutoSize"
    exit /b 1
)
echo Found: %COM_PORT%

:flash
cd /D "C:\Users\Onwuchekwa Valour\Desktop\TardOS\ESP32_OS_TardiOS\pocketos"
echo --- Flashing firmware + storage to %COM_PORT% ---
python "%IDF_PATH%\tools\idf.py" -p %COM_PORT% flash
if errorlevel 1 goto :fail

echo.
echo Flash OK. Run .\monitor.bat to see output.
goto :end

:fail
echo.
echo FLASH FAILED.
exit /b 1

:end
