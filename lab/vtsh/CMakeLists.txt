cmake_minimum_required(VERSION 3.12)

project(
    vtsh
    VERSION 0.1
    DESCRIPTION "VT Shell"
    LANGUAGES C
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(bin)
add_subdirectory(lib)

# ============================================
# Нагрузчики без оптимизации (для исследования)
# ============================================

# cpu-mat-mul: однопоточное перемножение матриц
add_executable(cpu-mat-mul loaders/cpu-mat-mul.c)
target_compile_options(cpu-mat-mul PRIVATE -O0 -g)

# ema-join-sm: Sort + Merge Join
add_executable(ema-join-sm loaders/ema-join-sm.c)
target_compile_options(ema-join-sm PRIVATE -O0 -g)

# ============================================
# Нагрузчики с агрессивной оптимизацией (-O3)
# ============================================

# cpu-mat-mul с оптимизацией
add_executable(cpu-mat-mul-opt loaders/cpu-mat-mul.c)
target_compile_options(cpu-mat-mul-opt PRIVATE -O3 -march=native -ffast-math)

# ema-join-sm с оптимизацией
add_executable(ema-join-sm-opt loaders/ema-join-sm.c)
target_compile_options(ema-join-sm-opt PRIVATE -O3 -march=native)

# ============================================
# Вспомогательные цели
# ============================================

# Цель для сборки всех нагрузчиков без оптимизации
add_custom_target(loaders
    DEPENDS cpu-mat-mul ema-join-sm
)

# Цель для сборки всех нагрузчиков с оптимизацией
add_custom_target(loaders-opt
    DEPENDS cpu-mat-mul-opt ema-join-sm-opt
)

# Цель для сборки всего
add_custom_target(all-loaders
    DEPENDS loaders loaders-opt
)
