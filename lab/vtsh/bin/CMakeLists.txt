cmake_minimum_required(VERSION 3.10)
project(vtsh_proj C)

add_executable(vtsh
    main.c
    builtins.c
)

target_include_directories(vtsh
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (TARGET libvtsh)
    target_link_libraries(vtsh PRIVATE libvtsh)
endif()

add_executable(cpu_sort        cpu_sort.c)
add_executable(ema_replace_int ema_replace_int.c)
add_executable(proc_fork       proc_fork.c)

# Общие настройки для всех утилит
foreach(tgt IN ITEMS cpu_sort ema_replace_int proc_fork)
    target_include_directories(${tgt}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if (TARGET libvtsh)
        target_link_libraries(${tgt} PRIVATE libvtsh)
    endif()
endforeach()
