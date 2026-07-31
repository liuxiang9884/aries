include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
    nova
    GIT_REPOSITORY git@github.com:dcfintech/nova.git
    GIT_TAG aa1b9f562f41e78442cef350c4fed01ecf33e8e8
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(nova)
