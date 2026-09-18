# PlayAmpNG — Dependências, versões e licenças

Atualizado no M0. Versões abaixo são as **verificadas na máquina de desenvolvimento**; o que vale para distribuição é medido no artefato empacotado (AU-08, M7).

## Máquina de referência do M0

| Item | Valor |
|---|---|
| SO | Arch Linux, kernel 7.2, x86_64 |
| Compilador | GCC 16.2.1 |
| CMake / Ninja | 4.4.3 / 1.13.2 |
| Backend de áudio detectado | PulseAudio (via PipeWire) |

## Dependências

| Dependência | Versão aqui | Licença | Papel | Como é obtida |
|---|---|---|---|---|
| Qt 6 Widgets | 6.11.2 | LGPL-3.0 (vínculo dinâmico) | Janela, eventos, diálogos, DPI, drag-and-drop, D-Bus, gravação atômica | Sistema, `find_package` |
| FFmpeg — libavformat | 63.1.101 | **ver aviso abaixo** | Demux, HTTP/HTTPS, ICY, seek | Sistema, `pkg-config` |
| FFmpeg — libavcodec | 63.1.101 | idem | Decodificação dos seis formatos | idem |
| FFmpeg — libavutil | 61.1.101 | idem | Utilitários | idem |
| FFmpeg — libswresample | 7.1.101 | idem | Reamostragem e conversão para f32 | idem |
| miniaudio | 0.11.25 | MIT-0 ou domínio público (dupla) | Enumeração de dispositivos e saída de áudio | `FetchContent`, tag fixada |
| Silkscreen | — | SIL Open Font License 1.1 | Fonte de pixel de toda a interface | Arquivo em `assets/skin/font/`, com `OFL.txt` ao lado |

Nenhuma outra dependência. DSP, FFT, playlist, parsers M3U/PLS e persistência são código próprio.

A Silkscreen substituiu um desenho próprio de 60 glifos em 5 × 7. A fonte desenhada à mão era grosseira, e existem fontes de pixel prontas, bem desenhadas e com licença permissiva — foi trabalho jogado fora. A OFL exige que a licença acompanhe o arquivo, e ela está em `assets/skin/font/OFL.txt`.

---

## ⚠ Aviso de licença do FFmpeg (risco #6 do PLAN.md, confirmado no M0)

O FFmpeg instalado nesta máquina reporta, em tempo de execução:

```
FFmpeg   n9.0.1  (licenca: GPL version 3 or later)
```

Ele foi compilado com `--enable-gpl --enable-version3`. **Vincular o binário distribuído contra esse build torna o produto inteiro GPL-3.0.**

Consequências práticas:

- **Desenvolvimento nesta máquina: sem problema.** O binário não é distribuído.
- **Empacotamento (M7): obrigatório usar um FFmpeg LGPL**, compilado sem `--enable-gpl` e sem `--enable-nonfree`, com vínculo dinâmico. Isso exclui componentes GPL (como `libx264`), que este projeto não usa — é um player de áudio.
- **Alternativa aceitável:** adotar GPL-3.0 para o projeto inteiro, decisão que precisa ser tomada explicitamente, não herdada por acidente do build da distro.

Por isso `pang_probe` imprime `avutil_license()` a cada execução, e o mesmo dado aparece na janela: a licença efetiva é uma propriedade do binário, não do código-fonte, e precisa ser verificável no artefato final.

**Pendência aberta:** decidir entre FFmpeg LGPL próprio e projeto GPL-3.0. Não bloqueia M1–M6.

---

## Codecs disponíveis nesta máquina

Verificados com `ffmpeg -decoders` no M0. A disponibilidade **no pacote distribuído** é outra coisa e será medida no M7 (AU-08).

| Formato | Decodificador | Presente aqui |
|---|---|---|
| MP3 | `mp3` | sim |
| WAV | `pcm_s16le` e variantes | sim |
| FLAC | `flac` | sim |
| Ogg Vorbis | `vorbis` | sim |
| Opus | `opus` | sim |
| AAC / M4A | `aac` | sim |

## Módulos do miniaudio desativados

`MA_NO_DECODING`, `MA_NO_ENCODING`, `MA_NO_GENERATION`. A decodificação é responsabilidade do FFmpeg; deixar os decodificadores do miniaudio ligados criaria dois caminhos para a mesma tarefa.
