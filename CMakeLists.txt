cmake_minimum_required(VERSION 3.16)

# Espressif HomeKit SDK (git submodule, pinned commit); its components live in these dirs.
set(HOMEKIT_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party/esp-homekit-sdk)
set(EXTRA_COMPONENT_DIRS ${HOMEKIT_SDK_PATH}/components/homekit)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tvlight)
