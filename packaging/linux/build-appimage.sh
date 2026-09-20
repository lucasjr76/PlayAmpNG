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

# O plugin qt embute so o backend que detecta em uso (aqui, xcb). Faltam:
#   wayland    sessao Wayland sem XWayland nao abriria o player
#   offscreen  sem ele o pacote nao pode ser verificado sem tela, e um pacote
#              que so da para testar na mao nao e verificavel
#
# Os NOMES variam por distribuicao — no Arch o Wayland e um arquivo so,
# libqwayland.so; no Debian sao libqwayland-generic.so e libqwayland-egl.so.
# A primeira versao desta linha trazia o nome do Arch fixo, e o empacotamento
# morria no Debian com "Cannot deploy non-existing library file". Agora a
# lista sai do que existe em disco.
diretorio_plugins="$("${QMAKE:-qmake6}" -query QT_INSTALL_PLUGINS)/platforms"
extras=""
for plugin in libqwayland libqwayland-generic libqwayland-egl \
              libqoffscreen libqminimal; do
  [ -e "$diretorio_plugins/$plugin.so" ] && extras="${extras:+$extras;}$plugin.so"
done

# Wayland ausente degradaria o pacote em silencio: ele continuaria sendo
# montado, e so nao abriria numa sessao Wayland pura — defeito que aparece na
# maquina do usuario e em nenhum log.
case "$extras" in
  *wayland*) ;;
  *) echo "ERRO: nenhum plugin Wayland em $diretorio_plugins (falta qt6-wayland?)" >&2
     exit 1 ;;
esac

export EXTRA_PLATFORM_PLUGINS="$extras"
echo "plugins de plataforma extras: $extras"
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
