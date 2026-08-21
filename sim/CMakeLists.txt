# Simulador de escritorio (SDL2): compila la MISMA UI y la misma logica que
# el firmware, con datos sinteticos. Iterar aqui es 10x mas rapido que
# flashear.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/sen66_sim
cmake_minimum_required(VERSION 3.16)
project(sen66_sim C)

set(CMAKE_C_STANDARD 11)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL_DIR ${REPO_ROOT}/managed_components/lvgl__lvgl)

if(NOT EXISTS ${LVGL_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
        "No existe ${LVGL_DIR}.\n"
        "Compila antes el firmware una vez (idf.py build) para que el gestor de "
        "componentes descargue LVGL.")
endif()

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON)
set(LV_CONF_BUILD_DISABLE_DEMOS ON)

add_subdirectory(${LVGL_DIR} lvgl)

find_package(SDL2 REQUIRED)
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(sen66_sim
    main.c
    ${REPO_ROOT}/main/air.c
    ${REPO_ROOT}/main/history.c
    ${REPO_ROOT}/main/ui.c
    ${REPO_ROOT}/main/i18n.c
    ${REPO_ROOT}/main/fonts/fonts.c
    ${REPO_ROOT}/main/fonts/font_de_14.c
    ${REPO_ROOT}/main/fonts/font_de_16.c
    ${REPO_ROOT}/main/fonts/font_de_22.c
    ${REPO_ROOT}/main/fonts/font_de_28.c
    ${REPO_ROOT}/main/fonts/font_de_48.c
)
target_include_directories(sen66_sim PRIVATE ${REPO_ROOT}/main)
target_link_libraries(sen66_sim lvgl m)
