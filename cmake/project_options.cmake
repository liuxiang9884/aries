include_guard(GLOBAL)

add_library(aries_project_options INTERFACE)
add_library(aries::project_options ALIAS aries_project_options)

target_compile_features(aries_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(
        aries_project_options
        INTERFACE
            /W4
            /permissive-
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        aries_project_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()
