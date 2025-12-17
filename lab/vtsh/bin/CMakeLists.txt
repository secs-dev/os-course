add_executable(
  shell
  ${CMAKE_SOURCE_DIR}/src/shell.c
)

target_include_directories(
  shell
  PRIVATE
  ${CMAKE_SOURCE_DIR}/lib
  ${CMAKE_SOURCE_DIR}/bin
  ${CMAKE_SOURCE_DIR}/src
)



target_link_libraries(
  shell
  PRIVATE
  libvtsh
)

# Ключевое: тесты запускают ./shell из lab/vtsh/test
set_target_properties(shell PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/test"
)
