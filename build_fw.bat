@echo off
set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.1"
set "IDF_TOOLS_PATH=C:\Espressif"
set "PATH=C:\Espressif\tools\cmake\3.24.0\bin;C:\Espressif\tools\ninja\1.11.1;C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;%PATH%"

cd /D "C:\Users\Onwuchekwa Valour\Desktop\TardOS\ESP32_OS_TardiOS\pocketos"

if not exist build (
    echo --- First run: configuring for ESP32-S3 ---
    python "%IDF_PATH%\tools\idf.py" set-target esp32s3
    if errorlevel 1 goto :fail
)

echo --- Building OS firmware ---
python "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 goto :fail

echo.
echo Build OK. Run flash_fw.bat COM14 to flash to the device.
goto :end

:fail
echo.
echo BUILD FAILED.
exit /b 1

:end
