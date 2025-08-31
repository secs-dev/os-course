add_library(
    vt
    STATIC
    cmp_file.cpp
    exception.cpp
    file.cpp
    log_file.cpp
)

target_include_directories(vt PUBLIC .)
target_link_libraries(vt PRIVATE vtpc)
