cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/firmware/main"
    "${CMAKE_CURRENT_LIST_DIR}/firmware/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sower_tinybee)
