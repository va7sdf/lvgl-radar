cmake_minimum_required(VERSION 3.18)
project(radar C)
set(CMAKE_C_STANDARD 99)

# LVGL from the submodule at the top of the repo, configured by ../lv_conf.h
# (and skip LVGL's examples and demos, which take ages to build on a Pi)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(../lvgl ${CMAKE_BINARY_DIR}/lvgl)

find_package(CURL REQUIRED)
find_library(CJSON_LIB cjson REQUIRED)
find_package(Threads REQUIRED)

add_executable(radar main.c config.c feed.c parse.c face.c map.c radar_ui.c ui.c lookup.c weather.c
               settings.c pistats.c receiver.c alerts.c units.c pages.c list.c)
target_compile_options(radar PRIVATE -Wall -Wextra)
target_link_libraries(radar PRIVATE lvgl CURL::libcurl ${CJSON_LIB} Threads::Threads m)
