# PlayAmpNG — Relatório de Execução de Testes

Registro incremental. Cada etapa acrescenta uma seção. Nada é marcado como aprovado sem ter sido executado.

Classificação: **AUTO** (executado por CTest) · **MANUAL** (procedimento executado por pessoa, resultado abaixo) · **NÃO VERIFICADO**.

---

## M0 — Fundação

Data: 2026-09-18 · Máquina: ver `DEPENDENCIES.md`

### Executado

| Verificação | Tipo | Resultado |
|---|---|---|
| Configuração e build limpos, do zero | MANUAL | **Passou** — `./build.sh clean`, 12 passos, sem avisos de erro |
| `ctest` executa | AUTO | **Passou** — 1/1 (`m0_headless`) |
| `pang_probe` roda sem Qt e sem janela | AUTO | **Passou** — binário headless não linka Qt |
| AR-07: o verificador de pureza roda a cada build | MANUAL | **Passou** — `AR-07 ok: 2 arquivo(s) em core/ sem Qt e sem API de SO` |
| AR-07: o verificador **falha** quando violado | MANUAL | **Passou** — `#include <QString>` inserido em `src/core/` interrompeu o build com `AR-07 violado` e a localização exata; arquivo removido em seguida |
| FFmpeg liga e decodifica cabeçalhos | MANUAL | **Passou** — probe em WAV, MP3 e Opus devolveu formato, codec, taxa, canais, duração, bitrate e seekable corretos |
| Reamostragem implícita observada | MANUAL | Opus reportou 48000 Hz contra 44100 Hz da fonte, como esperado |
| miniaudio liga e enumera dispositivos | MANUAL | **Passou** — backend PulseAudio, 5 dispositivos, padrão identificado |
| Arquivo ausente não derruba o processo | MANUAL | **Passou** — `ausente.flac` → `erro: nao foi possivel abrir: No such file or directory`, código de saída 0 |
| Janela Qt abre e permanece | MANUAL | **Passou** — `playampng` rodou até ser encerrado por timeout, exibindo versão do FFmpeg, licença e contagem de dispositivos |
| Licença efetiva do FFmpeg verificável em execução | MANUAL | **Passou** — reporta `GPL version 3 or later`; ver aviso em `DEPENDENCIES.md` |

### Requisitos atendidos nesta etapa

`AU-18`, `AU-19`, `VI-03`, `VI-14`, `EQ-10`, `AR-01`, `AR-07`, `EN-05` → `OK` (documentais e estruturais).

### Observações

1. **Risco #6 do `PLAN.md` confirmado no primeiro dia.** O FFmpeg do sistema é `--enable-gpl`. Não bloqueia o desenvolvimento, mas o empacotamento do M7 precisa de um build LGPL ou de uma decisão explícita por GPL-3.0. Registrado em `DEPENDENCIES.md`.

2. **miniaudio não é empacotado pela distro.** Obtido por `FetchContent` com tag fixada em 0.11.25, com `SOURCE_SUBDIR` apontando para um diretório inexistente — sem isso, o CMakeLists do próprio miniaudio entrava no build e compilava nodes de reverb, vocoder e decodificadores extras que não usamos (50 passos de build viraram 12).

3. **`qt_standard_project_setup()` liga AUTOMOC globalmente**, inclusive nos alvos que devem ser livres de Qt. Desligado explicitamente em `pang_core`, `pang_platform` e `pang_probe`. Sem isso, a separação de camadas passaria a existir só no papel.

4. **O `pang_probe` foi antecipado do M1 para o M0.** O plano previa descobrir surpresas de integração FFmpeg + miniaudio no M1; sessenta linhas de ferramenta headless anteciparam essa verificação sem custo relevante, e ela passou.

### Não verificado nesta etapa

Reprodução de áudio (nenhuma amostra chega ao dispositivo ainda), FLAC, Vorbis e AAC (só cabeçalhos de WAV, MP3 e Opus foram lidos), qualquer item de DSP, playlist, interface ou integração com o sistema.
