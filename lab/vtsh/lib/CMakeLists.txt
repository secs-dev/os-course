add_library(
    libvtsh
    STATIC
    vtsh.c
    shell.c
    redirection.c
    strfunc.c
)

target_include_directories(
    libvtsh
    PUBLIC
    .
)
