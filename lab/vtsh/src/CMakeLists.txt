add_executable(
    vtsh
    main.c
    vtsh.c
)

set_target_properties(vtsh PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(
    vtsh
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vtsh PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wstrict-prototypes
    )
endif()
