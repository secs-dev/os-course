add_executable(
    vtsh
    main.c
)

target_include_directories(
    vtsh
    PUBLIC
    .
)

target_link_libraries(
    vtsh
    PRIVATE
    libvtsh
)
