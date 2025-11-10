add_executable(
    cpu-calc-crc
    cpu-calc-crc.c
)

target_compile_options(cpu-calc-crc PRIVATE -O0)
