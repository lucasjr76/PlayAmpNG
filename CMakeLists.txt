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
find_package(ZLIB REQUIRED)   # leitor de .wsz; ja vem com o Qt

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
  src/core/audio/network.cpp
  src/core/audio/probe.cpp
  src/core/audio/decoder.cpp
  src/core/audio/engine.cpp
  src/core/dsp/analyzer.cpp
  src/core/dsp/biquad.cpp
  src/core/dsp/fft.cpp
  src/core/dsp/equalizer.cpp
  src/core/dsp/gain.cpp
  src/core/dsp/limiter.cpp
  src/core/dsp/presets.cpp
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
qt_add_executable(playampng
  src/ui/app.cpp
  src/ui/settings.cpp
  src/ui/shell/recovery.cpp
  src/ui/skin/winamp_skin.cpp
  src/ui/skin/zip.cpp
  src/ui/panel/main_panel.cpp
  src/ui/panel/equalizer_panel.cpp
  src/ui/panel/playlist_panel.cpp
  src/ui/shell/integrated.cpp)
target_link_libraries(playampng PRIVATE Qt6::Widgets ZLIB::ZLIB pang_core pang_platform)
target_compile_definitions(playampng PRIVATE
  PLAYAMPNG_VERSION="${PROJECT_VERSION}"
  PLAYAMPNG_SKIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/skin")

# Renderizador headless dos paineis, para medir alinhamento em pixel.
qt_add_executable(pang_render
  tools/render_panels.cpp
  src/ui/settings.cpp
  src/ui/shell/recovery.cpp
  src/ui/skin/winamp_skin.cpp
  src/ui/skin/zip.cpp
  src/ui/panel/main_panel.cpp
  src/ui/panel/equalizer_panel.cpp
  src/ui/panel/playlist_panel.cpp)
target_link_libraries(pang_render PRIVATE Qt6::Widgets ZLIB::ZLIB pang_core pang_platform)
target_compile_definitions(pang_render PRIVATE
  PLAYAMPNG_VERSION="${PROJECT_VERSION}"
  PLAYAMPNG_SKIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/skin")

# Extrai a fonte 5x6 do formato a partir da Silkscreen, para tools/make_wsz.py.
# Nao roda no build: a tabela e gerada uma vez e fica no gerador do skin.
qt_add_executable(pang_fontgen tools/fontgen.cpp)
target_link_libraries(pang_fontgen PRIVATE Qt6::Gui)
target_compile_definitions(pang_fontgen PRIVATE
  FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/skin/font/Silkscreen-Regular.ttf")

# ------------------------------------------------------------------- testes

# Dois testes precisam de Python para orquestrar o ambiente: o de rede sobe um
# servidor HTTP, o de perda de dispositivo cria e remove um sink.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

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

# M6-2 — dispositivo de saida. A politica de recuperacao e pura e roda sem
# placa de som; a enumeracao apenas exige consistencia, porque numa maquina de
# CI sem audio a lista vem vazia e isso nao e falha.
add_executable(pang_device_tests tests/test_device.cpp)
target_link_libraries(pang_device_tests PRIVATE pang_core pang_platform)
set_target_properties(pang_device_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME device COMMAND pang_device_tests)

# AU-12, RB-05 — perda REAL de dispositivo. Cria um sink virtual e o remove com
# o player tocando nele. Pulado (77) onde nao houver pactl nem servidor de
# audio: a ausencia deles numa maquina de integracao nao e defeito do player.
add_executable(pang_device_loss_tests tests/test_device_loss.cpp)
target_link_libraries(pang_device_loss_tests PRIVATE pang_core pang_platform)
set_target_properties(pang_device_loss_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME device_loss COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_device_loss.py
  $<TARGET_FILE:pang_device_loss_tests>)
set_tests_properties(device_loss PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Ida e volta da configuracao. Escreve em diretorio temporario proprio, via
# XDG_CONFIG_HOME, para nao tocar na configuracao real do usuario.
qt_add_executable(pang_settings_tests tests/test_settings.cpp src/ui/settings.cpp)
target_link_libraries(pang_settings_tests PRIVATE Qt6::Widgets pang_core)
add_test(NAME settings COMMAND pang_settings_tests)
set_tests_properties(settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M6-1 — fonte de rede. O teste sobe um servidor HTTP local de verdade; um
# mock nao exercitaria o que se quer verificar, que sao as decisoes do
# libavformat diante de cabecalho ICY, redirecionamento e conexao cortada.
add_executable(pang_stream_tests tests/test_stream.cpp)
target_link_libraries(pang_stream_tests PRIVATE pang_core)
target_compile_definitions(pang_stream_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_stream_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME stream COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_stream_test.py
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/stream_server.py
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/test
  $<TARGET_FILE:pang_stream_tests>)
# Prazo curto de proposito: a primeira versao deste teste TRAVOU, porque o
# libavformat reconectava sozinho e find_stream_info nunca via o fim do fluxo.
# Sem prazo, um travamento desses pendura a suite em vez de reprova-la.
set_tests_properties(stream PROPERTIES TIMEOUT 90)
set_tests_properties(audio PROPERTIES TIMEOUT 120)

add_executable(pang_playlist_tests tests/test_playlist.cpp)
target_link_libraries(pang_playlist_tests PRIVATE pang_core)
target_compile_definitions(pang_playlist_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_playlist_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_dsp_tests tests/test_dsp.cpp)
target_link_libraries(pang_dsp_tests PRIVATE pang_core)
set_target_properties(pang_dsp_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_gapless_tests tests/test_gapless.cpp)
target_link_libraries(pang_gapless_tests PRIVATE pang_core)
target_compile_definitions(pang_gapless_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_gapless_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_realtime_tests tests/test_realtime.cpp)
target_link_libraries(pang_realtime_tests PRIVATE pang_core)
target_compile_definitions(pang_realtime_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_realtime_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_vis_tests tests/test_visualization.cpp)
target_link_libraries(pang_vis_tests PRIVATE pang_core)
target_compile_definitions(pang_vis_tests PRIVATE
  PANG_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/test")
set_target_properties(pang_vis_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(pang_recovery_tests tests/test_recovery.cpp src/ui/shell/recovery.cpp)
target_include_directories(pang_recovery_tests PRIVATE src)
target_link_libraries(pang_recovery_tests PRIVATE Qt6::Core)
set_target_properties(pang_recovery_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME recovery COMMAND pang_recovery_tests)

add_executable(pang_layout_tests tests/test_layout.cpp)
target_include_directories(pang_layout_tests PRIVATE src)
target_link_libraries(pang_layout_tests PRIVATE Qt6::Core)
set_target_properties(pang_layout_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME layout COMMAND pang_layout_tests)

add_executable(pang_skin_tests
  tests/test_skin.cpp
  src/ui/skin/winamp_skin.cpp
  src/ui/skin/zip.cpp
  src/core/util/log.cpp)
target_include_directories(pang_skin_tests PRIVATE src)
target_link_libraries(pang_skin_tests PRIVATE Qt6::Gui ZLIB::ZLIB)
target_compile_definitions(pang_skin_tests PRIVATE
  PLAYAMPNG_SKIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/skin")
set_target_properties(pang_skin_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME skin COMMAND pang_skin_tests)
set_tests_properties(skin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME visualization COMMAND pang_vis_tests)
set_tests_properties(visualization PROPERTIES TIMEOUT 120)

add_test(NAME realtime COMMAND pang_realtime_tests)

add_test(NAME gapless COMMAND pang_gapless_tests)
set_tests_properties(gapless PROPERTIES TIMEOUT 120)

add_test(NAME dsp COMMAND pang_dsp_tests)
set_tests_properties(dsp PROPERTIES TIMEOUT 120)

add_test(NAME playlist COMMAND pang_playlist_tests)
set_tests_properties(playlist PROPERTIES TIMEOUT 120)

add_test(NAME m0_headless COMMAND pang_probe)
set_tests_properties(m0_headless PROPERTIES
  PASS_REGULAR_EXPRESSION "dispositivos de saida")
