# Codex: стенд только для QEMU, прошивки платы не изменяются.
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS "E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware/components/elf_loader" "E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware/components/cmake_utilities")
set(COMPONENTS main lwip cmake_utilities)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hmm2_runtime_qemu)
idf_component_get_property(loader_lib elf_loader COMPONENT_LIB)
target_link_libraries(${loader_lib} PRIVATE idf::lwip)
