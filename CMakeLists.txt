cmake_minimum_required(VERSION 3.24)
project(LINK VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LINK_ASAN "Enable AddressSanitizer on supported compilers" OFF)
find_package(Threads REQUIRED)

add_library(link_core
  native/src/core.cpp native/src/text.cpp native/src/loader.cpp
  native/src/script.cpp native/src/runtime.cpp native/src/navigation.cpp native/src/walk.cpp native/src/remote.cpp)
target_include_directories(link_core PUBLIC native/include)
target_link_libraries(link_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(link_core PUBLIC winhttp)
endif()
if(MSVC)
  target_compile_options(link_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
else()
  target_compile_options(link_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
if(LINK_ASAN)
  if(MSVC)
    target_compile_options(link_core PUBLIC /fsanitize=address /Zi)
    target_link_options(link_core PUBLIC /INCREMENTAL:NO)
  else()
    target_compile_options(link_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(link_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(WIN32)
  add_executable(LINK native/src/desktop.cpp native/src/textures.cpp)
  target_compile_definitions(LINK PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_libraries(LINK PRIVATE link_core opengl32 gdi32 user32 comctl32 shell32 windowscodecs ole32)
  add_custom_command(TARGET LINK POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/native/worlds" "$<TARGET_FILE_DIR:LINK>/worlds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/NOTICE" "${CMAKE_SOURCE_DIR}/ASSET_LICENSE.md"
      "$<TARGET_FILE_DIR:LINK>"
    VERBATIM)
  add_custom_target(link_worlds ALL COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/native/worlds" "$<TARGET_FILE_DIR:LINK>/worlds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/NOTICE" "${CMAKE_SOURCE_DIR}/ASSET_LICENSE.md"
      "$<TARGET_FILE_DIR:LINK>"
    DEPENDS LINK COMMENT "Synchronizing local worlds, photographs, and license notices"
    VERBATIM)
endif()

enable_testing()
add_executable(link_tests native/tests/main.cpp native/tests/parser_tests.cpp native/tests/script_tests.cpp native/tests/runtime_tests.cpp native/tests/exhibition_tests.cpp native/tests/walk_tests.cpp native/tests/guide_tests.cpp native/tests/network_tests.cpp)
target_link_libraries(link_tests PRIVATE link_core)
target_compile_definitions(link_tests PRIVATE LINK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/native")
add_test(NAME link_core_tests COMMAND link_tests)
set_tests_properties(link_core_tests PROPERTIES TIMEOUT 30)
add_executable(link_remote_loader_tests native/tests/remote_loader_tests.cpp)
target_link_libraries(link_remote_loader_tests PRIVATE link_core)
add_test(NAME link_remote_loader_tests COMMAND link_remote_loader_tests)
set_tests_properties(link_remote_loader_tests PROPERTIES TIMEOUT 30)

if(WIN32)
  add_executable(link_http_probe native/tests/http_probe.cpp)
  target_link_libraries(link_http_probe PRIVATE link_core)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME link_remote_transport COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/native/tests/network_integration.py"
      --probe "$<TARGET_FILE:link_http_probe>" --loader "$<TARGET_FILE:link_remote_loader_tests>"
      --worlds "${CMAKE_SOURCE_DIR}/native/worlds")
    set_tests_properties(link_remote_transport PROPERTIES TIMEOUT 60)
  endif()
endif()

if(WIN32)
  add_executable(link_image_tests native/tests/image_tests.cpp native/src/textures.cpp)
  target_link_libraries(link_image_tests PRIVATE link_core opengl32 gdi32 user32 windowscodecs ole32)
  target_compile_definitions(link_image_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN LINK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/native")
  add_test(NAME link_native_images COMMAND link_image_tests)
  set_tests_properties(link_native_images PROPERTIES TIMEOUT 30)
  add_test(NAME link_junction_confinement COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_SOURCE_DIR}/native/tests/junction-test.ps1"
    -Executable "$<TARGET_FILE:link_tests>" -BuildDirectory "${CMAKE_BINARY_DIR}")
  set_tests_properties(link_junction_confinement PROPERTIES TIMEOUT 30)
endif()
