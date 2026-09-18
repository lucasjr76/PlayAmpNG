cmake_minimum_required(VERSION 3.24)
project(PlayAmpNG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(CTest)
include(FetchContent)

# ---------------------------------------------------------------- dependencias

find_package(Threads REQUIRED)

# Qt entra apenas na camada ui/. Nunca em core/ nem em platform/.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# FFmpeg cobre demux, decode, resample, HTTP e ICY (spec 4 e 8).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswresample)

# miniaudio nao e empacotado pelas distros; versao fixada para build reprodutivel.
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.25
  GIT_SHALLOW    TRUE
  # So queremos o cabecalho. SOURCE_SUBDIR inexistente impede que o CMakeLists
  # do miniaudio entre no build e arraste nodes, reverb e decoders extras.
  SOURCE_SUBDIR  nao-construir)
FetchContent_MakeAvailable(miniaudio)

# ------------------------------------------------------- AR-07: pureza do core
# Roda a cada build, nao so na configuracao: um include de Qt ou de API de
# sistema operacional dentro de src/core/ falha a compilacao.
add_custom_target(core_purity ALL
  COMMAND ${CMAKE_COMMAND}
          -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/core
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_purity.cmake
  COMMENT "AR-07: verificando pureza de core/"
  VERBATIM)

# ------------------------------------------------------------------- alvos

# core/ : C++20 puro. Sem Qt, sem API de sistema operacional. Testavel headless.
add_library(pang_core STATIC
  src/core/audio/probe.cpp
  src/core/audio/decoder.cpp
  src/core/audio/engine.cpp
  src/core/dsp/biquad.cpp
  src/core/dsp/equalizer.cpp
  src/core/dsp/gain.cpp
  src/core/dsp/limiter.cpp
  src/core/meta/scanner.cpp
  src/core/meta/tags.cpp
  src/core/playlist/m3u.cpp
  src/core/playlist/playlist.cpp
  src/core/playlist/shuffle.cpp
  src/core/state/controller.cpp
  src/core/util/log.cpp)
target_include_directories(pang_core PUBLIC src)
target_link_libraries(pang_core PUBLIC PkgConfig::FFMPEG)
set_target_properties(pang_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_dependencies(pang_core core_purity)

# platform/ : unico lugar onde o sistema operacional aparece.
add_library(pang_platform STATIC
  src/platform/miniaudio_impl.c
  src/platform/audio_device.cpp)
target_include_directories(pang_platform
  PUBLIC  src
  PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pang_platform PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
  target_link_libraries(pang_platform PRIVATE m)
endif()
if(APPLE)
  target_link_libraries(pang_platform PRIVATE
    "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
set_target_properties(pang_platform PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Binario headless: prova que core/ e platform/ vivem sem Qt e sem janela.
add_executable(pang_probe tools/probe_main.cpp)
target_link_libraries(pang_probe PRIVATE pang_core pang_platform)
# Alvo headless: o AUTOMOC global do qt_standard_project_setup nao se aplica aqui.
set_target_properties(pang_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# ui/ : Qt. Janela provisoria ate o M5.
qt_add_executable(playampng src/ui/app.cpp)
target_link_libraries(playampng PRIVATE Qt6::Widgets pang_core pang_platform)
target_compile_definitions(playampng PRIVATE PLAYAMPNG_VERSION="${PROJECT_VERSION}")

# ------------------------------------------------------------------- testes

add_executable(pang_tests tests/test_core.cpp)
target_link_libraries(pang_tests PRIVATE pang_core pang_platform)
set_target_properties(pang_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME core COMMAND pang_tests)

add_executable(pang_audio_tests tests/test_audio.cpp)
target_link_libraries(pang_audio_tests PRIVATE pang_core)
target_compile_definitions(pang_audio_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_audio_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME audio COMMAND pang_audio_tests)
set_tests_properties(audio PROPERTIES TIMEOUT 120)

add_executable(pang_playlist_tests tests/test_playlist.cpp)
target_link_libraries(pang_playlist_tests PRIVATE pang_core)
target_compile_definitions(pang_playlist_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_playlist_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_dsp_tests tests/test_dsp.cpp)
target_link_libraries(pang_dsp_tests PRIVATE pang_core)
set_target_properties(pang_dsp_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME dsp COMMAND pang_dsp_tests)
set_tests_properties(dsp PROPERTIES TIMEOUT 120)

add_test(NAME playlist COMMAND pang_playlist_tests)
set_tests_properties(playlist PROPERTIES TIMEOUT 120)

add_test(NAME m0_headless COMMAND pang_probe)
set_tests_properties(m0_headless PROPERTIES
  PASS_REGULAR_EXPRESSION "dispositivos de saida")
