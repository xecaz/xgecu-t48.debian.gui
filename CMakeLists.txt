# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

project(xgecu-gui
    VERSION 0.2.0
    DESCRIPTION "Debian GUI for XGecu T48/T56/TL866II+ universal programmers"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test Concurrent)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# Build minipro static library via its existing Makefile
# ---------------------------------------------------------------------------

set(MINIPRO_DIR "${CMAKE_SOURCE_DIR}/third_party/minipro")
set(MINIPRO_LIB "${MINIPRO_DIR}/src/libminipro.a")

# A representative subset of minipro source files used for dependency tracking.
file(GLOB MINIPRO_SOURCES CONFIGURE_DEPENDS
    "${MINIPRO_DIR}/src/*.c"
    "${MINIPRO_DIR}/src/*.h"
    "${MINIPRO_DIR}/Makefile")

find_program(GNU_MAKE NAMES gmake make REQUIRED)
include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

add_custom_command(
    OUTPUT "${MINIPRO_LIB}"
    COMMAND ${CMAKE_COMMAND} -E echo "Building libminipro.a via minipro's Makefile"
    COMMAND ${GNU_MAKE} -j${NPROC} -C "${MINIPRO_DIR}" library
        CC=${CMAKE_C_COMPILER}
        "CFLAGS=-O2 -fPIC -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter"
    WORKING_DIRECTORY "${MINIPRO_DIR}"
    DEPENDS ${MINIPRO_SOURCES}
    VERBATIM
    COMMENT "make -C ${MINIPRO_DIR} library")

add_custom_target(minipro_build DEPENDS "${MINIPRO_LIB}")

add_library(minipro STATIC IMPORTED GLOBAL)
add_dependencies(minipro minipro_build)
set_target_properties(minipro PROPERTIES
    IMPORTED_LOCATION "${MINIPRO_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${MINIPRO_DIR}/src")
target_link_libraries(minipro INTERFACE PkgConfig::LIBUSB ZLIB::ZLIB)

# ---------------------------------------------------------------------------
# Generate chips_merged.json at configure time if missing
# ---------------------------------------------------------------------------

set(CHIPS_JSON "${CMAKE_SOURCE_DIR}/data/chips_merged.json")
set(MERGE_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/merge_chip_lists.py")
set(VENV_PY "${CMAKE_SOURCE_DIR}/.venv/bin/python")
set(T48_LIST "${CMAKE_SOURCE_DIR}/../T48_List.txt")

if(NOT EXISTS "${CHIPS_JSON}")
    if(EXISTS "${VENV_PY}")
        message(STATUS "Generating ${CHIPS_JSON} via merge_chip_lists.py")
        execute_process(
            COMMAND "${VENV_PY}" "${MERGE_SCRIPT}"
                --infoic "${MINIPRO_DIR}/infoic.xml"
                --t48list "${T48_LIST}"
                --out "${CHIPS_JSON}"
            RESULT_VARIABLE _merge_rc)
        if(NOT _merge_rc EQUAL 0)
            message(WARNING "merge_chip_lists.py failed (rc=${_merge_rc})")
        endif()
    else()
        message(WARNING "${VENV_PY} not found; run scripts/merge_chip_lists.py manually")
    endif()
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

qt_add_executable(xgecu-gui
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ZifSocketView.cpp
    src/ui/ZifSocketView.h
    src/ui/ChipSelectDialog.cpp
    src/ui/ChipSelectDialog.h
    src/ui/HexView.cpp
    src/ui/HexView.h
    src/ui/FuseEditorWidget.cpp
    src/ui/FuseEditorWidget.h
    src/core/ChipDatabase.cpp
    src/core/ChipDatabase.h
    src/core/Programmer.cpp
    src/core/Programmer.h
    src/core/ProgrammerWorker.cpp
    src/core/ProgrammerWorker.h
    src/core/BufferModel.cpp
    src/core/BufferModel.h
)

qt_add_resources(xgecu-gui "chips_data"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES "${CHIPS_JSON}")

target_include_directories(xgecu-gui PRIVATE src)
target_link_libraries(xgecu-gui PRIVATE
    Qt6::Widgets Qt6::Concurrent
    minipro)

# ---------------------------------------------------------------------------
# Installation rules — consumed by debhelper for the .deb build, and usable
# directly via `cmake --install build --prefix /usr/local`.
# ---------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS xgecu-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# minipro's chip XML databases — loaded at runtime via setInfoicPath /
# setLogicicPath, looked up under /usr/share/xgecu-gui/ at install.
install(FILES
        "${MINIPRO_DIR}/infoic.xml"
        "${MINIPRO_DIR}/logicic.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/xgecu-gui)

install(FILES "${CMAKE_SOURCE_DIR}/packaging/xgecu-gui.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES "${CMAKE_SOURCE_DIR}/packaging/xgecu-gui.svg"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# udev rules ship via the OS distribution path (/lib/udev/rules.d) so
# systemd-udevd picks them up regardless of /usr/local etc.
install(FILES
        "${MINIPRO_DIR}/udev/60-minipro.rules"
        "${MINIPRO_DIR}/udev/61-minipro-uaccess.rules"
    DESTINATION lib/udev/rules.d)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
