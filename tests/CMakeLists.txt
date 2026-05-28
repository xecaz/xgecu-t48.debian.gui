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
