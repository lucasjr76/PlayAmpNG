#!/usr/bin/env bash
# EN-04 — monta o AppImage.
#
# O AppImage leva as bibliotecas junto, INCLUSIVE o FFmpeg. Como o FFmpeg desta
# distribuicao e GPL-3.0 e o projeto adotou GPL-3.0-or-later (DEPENDENCIES.md),
# isso e coerente: a licenca do pacote e a mesma do codigo.
#
#   packaging/linux/build-appimage.sh [diretorio-de-saida]
#
# Baixa linuxdeploy e appimagetool na primeira execucao. Nao exige root.
set -euo pipefail

raiz="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
saida="${1:-$raiz/dist}"
trabalho="$(mktemp -d)"
trap 'rm -rf "$trabalho"' EXIT

ferramentas="${PANG_TOOLS_DIR:-$trabalho/tools}"
mkdir -p "$ferramentas" "$saida"

baixar() {
  local nome="$1" url="$2"
  [ -x "$ferramentas/$nome" ] && return 0
  echo "baixando $nome"
  curl -sL -o "$ferramentas/$nome" "$url"
  chmod +x "$ferramentas/$nome"
}

baixar linuxdeploy-x86_64.AppImage     https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
baixar linuxdeploy-plugin-qt-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
baixar appimagetool-x86_64.AppImage    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

echo "compilando"
cmake -S "$raiz" -B "$trabalho/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$trabalho/build" --target playampng -j"$(nproc)" >/dev/null

echo "instalando no AppDir"
cmake --install "$trabalho/build" --prefix "$trabalho/AppDir/usr" >/dev/null

# linuxdeploy resolve as bibliotecas do binario; o plugin qt traz os plugins de
# plataforma, sem os quais a janela nao abre em maquina sem Qt instalado.
# linuxdeploy procura o plugin no PATH, pelo nome canonico do arquivo.
export PATH="$ferramentas:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"

# O `strip` embutido no linuxdeploy nao entende a secao .relr.dyn que as
# toolchains atuais produzem, e falha em cada biblioteca. Sem strip o AppImage
# fica alguns megabytes maior e funciona; com ele, nao se monta.
export NO_STRIP=1

# O plugin qt embute so o backend que detecta em uso (aqui, xcb). Faltavam dois:
#   wayland    sessao Wayland sem XWayland nao abriria o player
#   offscreen  sem ele o pacote nao pode ser verificado sem tela, e um pacote
#              que so da para testar na mao nao e verificavel
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so;libqminimal.so"
export OUTPUT="$saida/PlayAmpNG-x86_64.AppImage"
export VERSION="${VERSION:-$(grep -oP 'VERSION \K[0-9.]+' "$raiz/CMakeLists.txt" | head -1)}"

"$ferramentas/linuxdeploy-x86_64.AppImage" \
  --appdir "$trabalho/AppDir" \
  --plugin qt \
  --desktop-file "$raiz/packaging/linux/br.com.playampng.PlayAmpNG.desktop" \
  --icon-file "$raiz/packaging/linux/icon_256.png" \
  --icon-filename br.com.playampng.PlayAmpNG \
  --output appimage

echo "pronto: $OUTPUT"
