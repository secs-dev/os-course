add_executable(
    vtsh
    main.c
)

target_include_directories(
    vtsh
    PUBLIC
    ..
)

target_link_libraries(
    vtsh
    PRIVATE
    libvtsh
)

add_custom_command(
    TARGET vtsh
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_SOURCE_DIR}/shell
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:vtsh> ${CMAKE_SOURCE_DIR}/shell
    VERBATIM
)
