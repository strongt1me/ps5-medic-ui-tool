cmake_minimum_required(VERSION 3.16)
project(ps5_medic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(medic STATIC
  libmedic/src/facts.cpp
  libmedic/src/posix_fs.cpp
  libmedic/src/snapshot_platform.cpp
  libmedic/src/snapshot_export.cpp
  libmedic/src/collectors.cpp
  libmedic/src/collectors_stack.cpp
  libmedic/src/rules.cpp
  libmedic/src/checks.cpp
  libmedic/src/games.cpp
  libmedic/src/history.cpp
  libmedic/src/selftest.cpp
  libmedic/src/error_codes.cpp
  libmedic/src/exfat.cpp
  libmedic/src/image_reader.cpp
  libmedic/src/imgread.c
  third_party/libdeflate/lib/adler32.c
  third_party/libdeflate/lib/deflate_decompress.c
  third_party/libdeflate/lib/zlib_decompress.c
  third_party/libdeflate/lib/utils.c
  third_party/libdeflate/lib/x86/cpu_features.c
  libmedic/src/text_policy.cpp
  libmedic/src/sqlite_check.cpp
  libmedic/src/sha256.cpp
  libmedic/src/ini.cpp
  libmedic/src/fixes.cpp
  libmedic/src/engine.cpp
  libmedic/src/diff.cpp
  libmedic/src/report.cpp
  libmedic/src/rule_update.cpp
  libmedic/src/qr.cpp
  libmedic/src/ed25519.cpp
  libmedic/src/signature.cpp
  libmedic/src/trusted_keys.cpp
  libmedic/src/http.cpp
  libmedic/src/posix_http.cpp
  libmedic/src/rule_download.cpp
  third_party/qrcodegen/qrcodegen.cpp
  third_party/tweetnacl/tweetnacl.c
  third_party/sqlite3/sqlite3.c)
target_include_directories(medic PUBLIC libmedic/include third_party)
target_compile_options(medic PRIVATE -Wall -Wextra)
set_source_files_properties(third_party/qrcodegen/qrcodegen.cpp third_party/tweetnacl/tweetnacl.c third_party/libdeflate/lib/adler32.c third_party/libdeflate/lib/deflate_decompress.c third_party/libdeflate/lib/zlib_decompress.c third_party/libdeflate/lib/utils.c third_party/libdeflate/lib/x86/cpu_features.c PROPERTIES COMPILE_OPTIONS "-w")  # Fremdcode unveraendert lassen
# SQLite ohne Dateizugriff (nur Speicher-Datenbanken, siehe libmedic/src/sqlite_check.cpp)
set_source_files_properties(third_party/sqlite3/sqlite3.c PROPERTIES COMPILE_OPTIONS "-w;-O1"
  COMPILE_DEFINITIONS "SQLITE_OS_OTHER=1;SQLITE_THREADSAFE=0;SQLITE_TEMP_STORE=3;SQLITE_OMIT_LOAD_EXTENSION=1;SQLITE_OMIT_SHARED_CACHE=1;SQLITE_OMIT_DEPRECATED=1;SQLITE_OMIT_LOCALTIME=1;SQLITE_OMIT_PROGRESS_CALLBACK=1;SQLITE_DEFAULT_MEMSTATUS=0;SQLITE_DQS=0;SQLITE_MAX_MMAP_SIZE=0")

# Plattformunabhaengige UI (Canvas, Schrift, Bildschirme). Die PS5-Anbindung (VideoOut, Controller) kommt separat.
add_library(medic_ui STATIC
  ui/src/canvas.cpp
  ui/src/font.cpp
  ui/src/font_data.cpp
  ui/src/i18n.cpp
  ui/src/i18n_embedded.cpp
  ui/src/icons.cpp
  ui/src/png.cpp
  ui/src/shot_hook.cpp
  ui/src/splash.cpp
  ui/src/app.cpp
  ui/src/local_service.cpp)
target_include_directories(medic_ui PUBLIC ui/include PRIVATE ui/src third_party)
set_source_files_properties(ui/src/splash.cpp PROPERTIES COMPILE_DEFINITIONS "MEDIC_SPLASH_PATH=\"${CMAKE_SOURCE_DIR}/ui/assets/splash.jpg\";MEDIC_BACKGROUND_PATH=\"${CMAKE_SOURCE_DIR}/ui/assets/background.jpg\"" OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/ui/assets/splash.jpg;${CMAKE_SOURCE_DIR}/ui/assets/background.jpg")
target_link_libraries(medic_ui PUBLIC medic)
target_compile_options(medic_ui PRIVATE -Wall -Wextra)

add_executable(medic-ui-preview ui/preview/main.cpp)
target_link_libraries(medic-ui-preview PRIVATE medic_ui)
target_compile_definitions(medic-ui-preview PRIVATE MEDIC_DEFAULT_RULES="${CMAKE_SOURCE_DIR}/knowledge/rules.json"
  MEDIC_DEFAULT_ERROR_CODES="${CMAKE_SOURCE_DIR}/knowledge/error_codes.json")

add_executable(medic-fuzz-image tests/fuzz_image.cpp)
target_link_libraries(medic-fuzz-image PRIVATE medic)
target_compile_definitions(medic-fuzz-image PRIVATE MEDIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_executable(medic-sign tools/medic_sign.cpp)
target_link_libraries(medic-sign PRIVATE medic)

add_executable(medic-cli cli/main.cpp)
target_link_libraries(medic-cli PRIVATE medic)
target_compile_definitions(medic-cli PRIVATE MEDIC_DEFAULT_RULES="${CMAKE_SOURCE_DIR}/knowledge/rules.json")

enable_testing()
add_executable(medic-tests tests/test_main.cpp tests/test_fixes.cpp tests/test_ui.cpp tests/test_report.cpp tests/test_update.cpp tests/test_sign.cpp tests/test_error_codes.cpp tests/test_codes_update.cpp tests/test_text_policy.cpp tests/test_history.cpp tests/test_selftest.cpp)
find_package(Threads REQUIRED)
target_link_libraries(medic-tests PRIVATE medic_ui Threads::Threads)
target_compile_definitions(medic-tests PRIVATE
  MEDIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME medic-tests COMMAND medic-tests)
