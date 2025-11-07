add_library(libvtsh STATIC
    vtsh.c
    vtsh_utils.c
    vtsh_parser.c
    vtsh_executor.c
    vtsh_signals.c
)

target_include_directories(libvtsh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(libvtsh PROPERTIES OUTPUT_NAME vtsh)
