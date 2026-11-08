cmake_minimum_required(VERSION 3.16)
project(ps5_medic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(medic STATIC
  libmedic/src/facts.cpp
  libmedic/src/posix_fs.cpp
  libmedic/src/snapshot_platform.cpp
  libmedic/src/snapshot_export.cpp
  libmedic/src/collectors.cpp
  libmedic/src/rules.cpp
  libmedic/src/sha256.cpp
  libmedic/src/ini.cpp
  libmedic/src/fixes.cpp
  libmedic/src/engine.cpp
  libmedic/src/diff.cpp)
target_include_directories(medic PUBLIC libmedic/include third_party)
target_compile_options(medic PRIVATE -Wall -Wextra)

# Plattformunabhaengige UI (Canvas, Schrift, Bildschirme). Die PS5-Anbindung (VideoOut, Controller) kommt separat.
add_library(medic_ui STATIC
  ui/src/canvas.cpp
  ui/src/font.cpp
  ui/src/font_data.cpp
  ui/src/i18n.cpp
  ui/src/i18n_embedded.cpp
  ui/src/png.cpp
  ui/src/app.cpp
  ui/src/local_service.cpp)
target_include_directories(medic_ui PUBLIC ui/include PRIVATE ui/src)
target_link_libraries(medic_ui PUBLIC medic)
target_compile_options(medic_ui PRIVATE -Wall -Wextra)

add_executable(medic-ui-preview ui/preview/main.cpp)
target_link_libraries(medic-ui-preview PRIVATE medic_ui)
target_compile_definitions(medic-ui-preview PRIVATE MEDIC_DEFAULT_RULES="${CMAKE_SOURCE_DIR}/knowledge/rules.json")

add_executable(medic-cli cli/main.cpp)
target_link_libraries(medic-cli PRIVATE medic)
target_compile_definitions(medic-cli PRIVATE MEDIC_DEFAULT_RULES="${CMAKE_SOURCE_DIR}/knowledge/rules.json")

enable_testing()
add_executable(medic-tests tests/test_main.cpp tests/test_fixes.cpp tests/test_ui.cpp)
target_link_libraries(medic-tests PRIVATE medic_ui)
target_compile_definitions(medic-tests PRIVATE
  MEDIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME medic-tests COMMAND medic-tests)
