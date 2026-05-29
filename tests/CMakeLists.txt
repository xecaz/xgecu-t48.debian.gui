# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_executable(test_chip_database test_chip_database.cpp
    ../src/core/ChipDatabase.cpp ../src/core/ChipDatabase.h)
target_link_libraries(test_chip_database PRIVATE Qt6::Test Qt6::Widgets)
target_include_directories(test_chip_database PRIVATE ../src)
add_test(NAME chip_database COMMAND test_chip_database)
set_tests_properties(chip_database PROPERTIES
    ENVIRONMENT "CHIPS_JSON=${CMAKE_SOURCE_DIR}/data/chips_merged.json")

qt_add_executable(test_buffer_model test_buffer_model.cpp
    ../src/core/BufferModel.cpp ../src/core/BufferModel.h)
target_link_libraries(test_buffer_model PRIVATE Qt6::Test Qt6::Widgets)
target_include_directories(test_buffer_model PRIVATE ../src)
add_test(NAME buffer_model COMMAND test_buffer_model)

# Live-hardware smoke tests. Links against the real Programmer + Worker
# (which means libminipro + libusb). Each test method calls QSKIP unless
# XGECU_LIVE_TESTS=1 is set in the environment, so it's safe to keep
# enabled in the default ctest run.
qt_add_executable(test_live_programmer live/test_live_programmer.cpp
    ../src/core/Programmer.cpp ../src/core/Programmer.h
    ../src/core/ProgrammerWorker.cpp ../src/core/ProgrammerWorker.h)
target_compile_definitions(test_live_programmer PRIVATE
    SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_live_programmer PRIVATE
    Qt6::Test Qt6::Concurrent Qt6::Widgets minipro)
target_include_directories(test_live_programmer PRIVATE ../src)
add_test(NAME live_programmer COMMAND test_live_programmer)
