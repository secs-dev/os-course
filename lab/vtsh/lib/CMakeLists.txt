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
target_sources(vtsh PRIVATE myshell.c)
