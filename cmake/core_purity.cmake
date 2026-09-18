# AR-07 — core/ nao pode depender de Qt nem do sistema operacional.
#
# Essa e a regra que faz o port para Windows (M8) caber em platform/. Ela custa
# quase nada agora e custa um refactor inteiro se for lembrada tarde.
#
# Uso: cmake -DCORE_DIR=<dir> -P core_purity.cmake

if(NOT DEFINED CORE_DIR)
  message(FATAL_ERROR "core_purity: CORE_DIR nao definido")
endif()

set(BANNED
  "#[ \t]*include[ \t]*[<\"]Q"          # qualquer cabecalho Qt
  "#[ \t]*include[ \t]*<windows\\.h>"
  "#[ \t]*include[ \t]*<unistd\\.h>"
  "#[ \t]*include[ \t]*<sys/"
  "#[ \t]*include[ \t]*<dbus/"
  "#[ \t]*include[ \t]*[<\"]miniaudio"   # saida de audio pertence a platform/
  "#[ \t]*if(def)?[ \t]+_WIN32"
  "#[ \t]*if(def)?[ \t]+__linux__"
  "#[ \t]*if(def)?[ \t]+__APPLE__"
)

file(GLOB_RECURSE CORE_FILES "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.cpp")

set(VIOLATIONS "")
foreach(f IN LISTS CORE_FILES)
  file(STRINGS "${f}" lines)
  set(n 0)
  foreach(line IN LISTS lines)
    math(EXPR n "${n}+1")
    foreach(pat IN LISTS BANNED)
      if(line MATCHES "${pat}")
        list(APPEND VIOLATIONS "${f}:${n}: ${line}")
      endif()
    endforeach()
  endforeach()
endforeach()

if(VIOLATIONS)
  string(REPLACE ";" "\n  " pretty "${VIOLATIONS}")
  message(FATAL_ERROR
    "AR-07 violado: core/ depende de Qt ou do sistema operacional.\n  ${pretty}\n"
    "Mova isso para src/platform/ ou src/ui/.")
endif()

list(LENGTH CORE_FILES nfiles)
message(STATUS "AR-07 ok: ${nfiles} arquivo(s) em core/ sem Qt e sem API de SO")
