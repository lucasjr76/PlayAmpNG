#!/usr/bin/env bash
# EN-10 — monta o PlayAmpNG.app e o .dmg.
#
#   packaging/macos/build-dmg.sh [diretorio-de-build] [diretorio-de-saida]
#
# Espera Qt (macdeployqt no PATH ou em $(brew --prefix qt@6)/bin) e FFmpeg
# instalados. Nao assina nem notariza: isso exige conta de desenvolvedor Apple,
# e e decisao a parte (EN-10). Sem assinatura, o macOS pede confirmacao na
# primeira abertura.
#
# Duas conferencias no fim, porque os defeitos de pacote nao aparecem na
# maquina que o monta — o Windows ja provou isso tres vezes:
#   1. nenhuma biblioteca pode apontar para a instalacao da maquina de build
#      (/opt/homebrew, /usr/local): no Mac do usuario ela nao existe;
#   2. o player aberto a partir do pacote precisa achar o skin DENTRO dele, e
#      nao no codigo-fonte, que existe aqui e em nenhum outro lugar.
set -euo pipefail

raiz="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$raiz/build}"
saida="${2:-$raiz/dist}"
mkdir -p "$saida"

cmake -S "$raiz" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$build" --target playampng

app="$saida/PlayAmpNG.app"
rm -rf "$app"
cp -R "$build/playampng.app" "$app"

recursos="$app/Contents/Resources"
mkdir -p "$recursos/skin"
cp -R "$raiz/assets/skin/default" "$recursos/skin/default"
cp "$raiz/LICENSE" "$recursos/"
cp "$raiz/assets/skin/font/OFL.txt" "$recursos/OFL-Silkscreen.txt"

deploy="$(command -v macdeployqt || echo "$(brew --prefix qt@6)/bin/macdeployqt")"
"$deploy" "$app" -always-overwrite

# ---------------------------------------------------- conferencia 1: vinculos
echo "conferindo vinculos"
falhas=0
while IFS= read -r arquivo; do
    if otool -L "$arquivo" | tail -n +2 | grep -E '(/opt/homebrew|/usr/local)/' >/dev/null; then
        echo "  aponta para a maquina de build: ${arquivo#$app/}"
        otool -L "$arquivo" | grep -E '(/opt/homebrew|/usr/local)/' | sed 's/^/      /'
        falhas=$((falhas + 1))
    fi
done < <(find "$app/Contents" -type f \( -name '*.dylib' -o -perm -u+x \) -exec sh -c 'file "$1" | grep -q Mach-O && echo "$1"' _ {} \;)
if [ "$falhas" -gt 0 ]; then
    echo "ERRO: $falhas arquivo(s) dependem de bibliotecas fora do pacote" >&2
    exit 1
fi
echo "  nenhum vinculo para fora do pacote"

# ----------------------------------------------- conferencia 2: skin do pacote
# Le o log do stderr, e nao do arquivo: no macOS o Qt grava a configuracao em
# ~/Library, calculado sem olhar HOME, e procurar o arquivo seria procurar no
# lugar errado. O stderr recebe as mesmas linhas.
#
# Com o plugin de janela do proprio macOS (cocoa), e nao offscreen: testa o que
# o usuario vai usar, e o pacote nao carrega um plugin so para ser conferido.
saida_player="$(mktemp)"
trap 'rm -f "$saida_player"' EXIT
"$app/Contents/MacOS/playampng" 2>"$saida_player" &
pid=$!
for _ in $(seq 100); do
    grep -q 'skin:' "$saida_player" && break
    sleep 0.1
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if ! grep -q 'skin:.*/Contents/Resources/skin/default' "$saida_player"; then
    echo "ERRO: o player aberto do pacote nao carregou o skin de dentro dele" >&2
    sed 's/^/    /' "$saida_player" >&2
    exit 1
fi
echo "  skin carregado de dentro do pacote"

versao="$(grep -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' "$raiz/CMakeLists.txt" | head -1 | cut -d' ' -f2)"
dmg="$saida/PlayAmpNG-$versao-macos-$(uname -m).dmg"
rm -f "$dmg"
hdiutil create -volname "PlayAmpNG $versao" -srcfolder "$app" -ov -format UDZO "$dmg" >/dev/null
echo "pronto: $dmg"
