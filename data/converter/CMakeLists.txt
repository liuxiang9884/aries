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

add_library(
    aries_taifex_converter
    taifex/bcd_decoder.cpp
    taifex/csv_writer.cpp
    taifex/dump_converter.cpp
    taifex/message_decoder.cpp
)

target_include_directories(
    aries_taifex_converter
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)

target_link_libraries(
    aries_taifex_converter
    PUBLIC
        aries::project_options
    PRIVATE
        fmt::fmt
)

add_executable(
    taifex_dump_converter
    taifex/taifex_dump_converter_main.cpp
)

target_link_libraries(
    taifex_dump_converter
    PRIVATE
        aries::project_options
        aries_taifex_converter
        CLI11::CLI11
        nova
)

if(BUILD_TESTING)
    add_test(
        NAME taifex_dump_converter_help
        COMMAND taifex_dump_converter --help
    )
endif()
