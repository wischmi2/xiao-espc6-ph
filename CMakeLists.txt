cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS submodules/golioth-firmware-sdk/port/esp_idf/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao-espc6-ph)
