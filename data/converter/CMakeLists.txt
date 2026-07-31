find_package(CLI11 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_library(
    aries_twse_converter
    twse/basic_info.cpp
    twse/bcd_decoder.cpp
    twse/csv_writer.cpp
    twse/dump_converter.cpp
    twse/message_decoder.cpp
)

target_include_directories(
    aries_twse_converter
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)

target_link_libraries(
    aries_twse_converter
    PUBLIC
        aries::project_options
    PRIVATE
        fmt::fmt
)

add_executable(
    twse_dump_converter
    twse/twse_dump_converter_main.cpp
)

target_link_libraries(
    twse_dump_converter
    PRIVATE
        aries::project_options
        aries_twse_converter
        CLI11::CLI11
        nova
)

if(BUILD_TESTING)
    add_test(
        NAME twse_dump_converter_help
        COMMAND twse_dump_converter --help
    )
endif()
