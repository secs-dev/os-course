set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(lib)

add_executable(test_basic test_basic.cpp)
target_include_directories(test_basic PUBLIC .)
target_link_libraries(test_basic PRIVATE vt)

add_executable(test_seq test_seq.cpp)
target_include_directories(test_seq PUBLIC .)
target_link_libraries(test_seq PRIVATE vt)

add_executable(test_random test_random.cpp)
target_include_directories(test_random PUBLIC .)
target_link_libraries(test_random PRIVATE vt)
