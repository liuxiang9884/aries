find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
