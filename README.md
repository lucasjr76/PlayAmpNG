# PlayAmpNG

Reprodutor de áudio para desktop com a diagramação e as proporções do formato de skin do **Winamp 2.x**: painéis de tamanho fixo, fonte bitmap, analisador de espectro e equalizador de dez bandas.

Toca MP3, WAV, FLAC, Ogg Vorbis, Opus e AAC — local ou por streaming HTTP — com reprodução sem lacuna, ReplayGain e integração MPRIS.

**Licença: GPL-3.0-or-later** (`LICENSE`). A razão da escolha está em [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md).

---

## Baixar e rodar

```sh
chmod +x PlayAmpNG-x86_64.AppImage
./PlayAmpNG-x86_64.AppImage
```

O AppImage carrega as bibliotecas de que precisa, inclusive Qt e FFmpeg. Não instala nada e não precisa de root.

## Compilar a partir da fonte

### Dependências

| Item | Versão mínima | Pacote (Arch) | Pacote (Debian/Ubuntu) |
|---|---|---|---|
| CMake | 3.24 | `cmake` | `cmake` |
| Compilador C++20 | GCC 12 / Clang 15 | `gcc` | `g++` |
| Qt 6 (Widgets, Network, DBus) | 6.5 | `qt6-base` | `qt6-base-dev` |
| FFmpeg (libavformat, libavcodec, libavutil, libswresample) | 6.0 | `ffmpeg` | `libavformat-dev libavcodec-dev libavutil-dev libswresample-dev` |
| zlib | 1.2 | `zlib` | `zlib1g-dev` |
| Python 3 | 3.9 | `python` | `python3` |

`miniaudio` é buscado pelo CMake com `FetchContent` numa tag fixa; a primeira compilação precisa de rede.

### Compilar, testar, instalar

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr/local
```

A suíte roda sem tela e sem placa de som. Os testes que precisam de barramento D-Bus, servidor PulseAudio/PipeWire ou `pactl` são **pulados**, não reprovados, onde esses recursos não existirem.

### Gerar os pacotes

```sh
packaging/linux/build-appimage.sh          # -> dist/PlayAmpNG-x86_64.AppImage

flatpak install -y flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/linux/br.com.playampng.PlayAmpNG.yml
```

O script do AppImage baixa `linuxdeploy` e `appimagetool` na primeira execução e não exige root.

### Regerar a arte do skin

```sh
python3 tools/make_wsz.py          # bitmaps do formato + .wsz
cmake --build build --target pang_fontgen && ./build/pang_fontgen
```

A arte vive em código: qualquer ajuste é um diff legível. A tabela de glifos é **extraída da fonte Silkscreen** por `pang_fontgen`, e não desenhada à mão — três versões manuais saíram com forma de letra errada.

---

## Usar

| | |
|---|---|
| Atalhos de teclado | [`docs/ATALHOS.md`](docs/ATALHOS.md), e no programa: botão direito → *Atalhos de teclado* |
| Escolher a saída de áudio | botão direito na janela principal → *Dispositivo de saída* |
| Propriedades da faixa | botão direito → *Propriedades da faixa* |
| Trocar o skin | qualquer `.wsz` do Winamp 2.x, ou uma pasta com os bitmaps |

Teclas de mídia e controle pelo ambiente funcionam via MPRIS, sem a janela em foco.

---

## Documentação

| Arquivo | Conteúdo |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | camadas, contratos de thread, árvore do código |
| [`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) | matriz de requisitos, com estado e evidência |
| [`docs/TEST_REPORT.md`](docs/TEST_REPORT.md) | o que foi medido, com números e máquina |
| [`docs/DEFEITOS.md`](docs/DEFEITOS.md) | defeitos encontrados em uso, causa medida e correção |
| [`docs/LIMITACOES.md`](docs/LIMITACOES.md) | o que o player **não** faz, e por quê |
| [`docs/APROXIMACOES.md`](docs/APROXIMACOES.md) | onde a aparência diverge da referência, e a medição |
| [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) | versões, licenças e a decisão de GPL-3.0 |
| [`docs/PLAN.md`](docs/PLAN.md) | marcos, riscos e o que ficou fora |
