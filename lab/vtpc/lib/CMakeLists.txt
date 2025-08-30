add_library(
    vtpc
    STATIC
    vtpc.c
)

target_include_directories(
    vtpc
    PUBLIC
    .
)
