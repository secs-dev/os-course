add_library(
    libvtsh
    STATIC
    vtsh.c
)

target_include_directories(
    libvtsh
    PUBLIC
    .
)
