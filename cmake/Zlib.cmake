Include(FetchContent)

FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_PROGRESS TRUE
        GIT_SHALLOW TRUE
)