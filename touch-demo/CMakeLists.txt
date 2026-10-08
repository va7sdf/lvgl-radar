cmake_minimum_required(VERSION 3.18)
project(touch_demo C)
set(CMAKE_C_STANDARD 99)

# LVGL from the submodule at the top of the repo, configured by ../lv_conf.h
# (and skip LVGL's examples and demos, which take ages to build on a Pi)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(../lvgl ${CMAKE_BINARY_DIR}/lvgl)

add_executable(touch-demo main.c)
target_compile_options(touch-demo PRIVATE -Wall -Wextra)
target_link_libraries(touch-demo PRIVATE lvgl m)
