cmake_minimum_required(VERSION 3.20)
project(sworddeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XEXT REQUIRED xext)

add_executable(sworddeck
    src/main.cpp
    src/cyberdeck.cpp
    src/main_panel.cpp
    src/right_panel.cpp
    src/bottom_bar.cpp
    src/spectrum_overlay.cpp
    src/pipes_layer.cpp
    src/glava_manager.cpp
)

target_include_directories(sworddeck PRIVATE src ${XCB_INCLUDE_DIRS} ${X11_INCLUDE_DIRS} ${XEXT_INCLUDE_DIRS})
target_link_libraries(sworddeck PRIVATE
    Qt6::Widgets
    Qt6::Gui
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    ${XEXT_LIBRARIES}
    pthread
)

install(TARGETS sworddeck RUNTIME DESTINATION bin)
