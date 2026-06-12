# Builds the app as a relocatable ELF (not a flashable firmware image).
# The elf_loader component supplies include(elf_loader) and project_elf().
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello)
include(elf_loader)
project_elf(hello)
