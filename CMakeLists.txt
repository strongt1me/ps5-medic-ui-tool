cmake_minimum_required(VERSION 3.16)
project(ps5_medic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(medic STATIC
  libmedic/src/facts.cpp
  libmedic/src/snapshot_platform.cpp
  libmedic/src/collectors.cpp
  libmedic/src/rules.cpp)
target_include_directories(medic PUBLIC libmedic/include third_party)
target_compile_options(medic PRIVATE -Wall -Wextra)

add_executable(medic-cli cli/main.cpp)
target_link_libraries(medic-cli PRIVATE medic)
target_compile_definitions(medic-cli PRIVATE MEDIC_DEFAULT_RULES="${CMAKE_SOURCE_DIR}/knowledge/rules.json")

enable_testing()
add_executable(medic-tests tests/test_main.cpp)
target_link_libraries(medic-tests PRIVATE medic)
target_compile_definitions(medic-tests PRIVATE
  MEDIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME medic-tests COMMAND medic-tests)
