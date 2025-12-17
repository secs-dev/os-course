add_executable(
  vtsh
  ${CMAKE_SOURCE_DIR}/src/shell.c
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
