find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest CONFIG REQUIRED)

include(GoogleTest)

add_test(
    NAME synology_pull
    COMMAND
        ${Python3_EXECUTABLE}
        -m unittest
        -v
        tests/test_synology_pull.py
)

set_tests_properties(
    synology_pull
    PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
)

add_executable(
    twse_converter_tests
    data/converter/twse/bcd_decoder_test.cpp
    data/converter/twse/dump_converter_test.cpp
    data/converter/twse/message_decoder_test.cpp
)

target_include_directories(
    twse_converter_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}"
)

target_link_libraries(
    twse_converter_tests
    PRIVATE
        aries::project_options
        aries_twse_converter
        GTest::gtest_main
)

gtest_discover_tests(twse_converter_tests)
