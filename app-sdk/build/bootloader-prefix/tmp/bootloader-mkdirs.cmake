# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/ggold/esp/esp-idf/components/bootloader/subproject"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/tmp"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/src/bootloader-stamp"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/src"
  "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ggold/Documents/Platformio/personal/ESP32_OS_TardiOS/app-sdk/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
