# PlayAmpNG — Plano de Desenvolvimento

Versão 0.2 — revisão após crítica técnica do M0.

Alvos: **Linux, Windows e macOS**. Linux é o ambiente principal de desenvolvimento; os três são construídos e testados a cada etapa, desde o M0. Integração e distribuição específicas ficam no fim.

Cada etapa termina com **software executável** e com a verificação correspondente registrada em `TEST_REPORT.md`. Nenhuma etapa entrega placeholder visual.

---

## Etapas

### M0 — Fundação nos três sistemas

Entrega: janela que abre, build reprodutível e suíte rodando em Linux, Windows e macOS.

- CMake com FFmpeg, Qt 6, miniaudio (`FetchContent`, tag fixada).
- Regra de build AR-07: falha a compilação se `core/` incluir Qt ou API de sistema.
- `PANG_CHECK` — verificação que **não** desaparece com `NDEBUG`.
- `pang_probe`: binário headless que prova que `core/` e `platform/` vivem sem Qt.
- **CI com matriz de três sistemas**, rodando build + `ctest` em cada um.
- Documentos: `ARCHITECTURE.md`, `REQUIREMENTS.md`, `PLAN.md`, `DEPENDENCIES.md`.

Verificação: build limpo e `ctest` verde nos três; `pang_probe` enumera dispositivos e lê cabeçalhos nos três; o verificador AR-07 falha quando violado.

Cobre: AU-18, AU-19, VI-03, VI-14, EQ-10, EQ-13, AR-01, AR-07, EN-05.

**Estado:** concluído em Linux. Windows e macOS pendentes de CI — ver riscos #11 e #12.

---

### M1 — Reprodução real

Entrega: player de linha de comando com janela provisória. **Toca som de verdade**, nos três sistemas.

- `core/audio/decoder` — libav: demux, decode, resample para f32 na taxa do dispositivo.
- **I/O cancelável desde já**: `AVIOInterruptCB` + `rw_timeout`. Parar, trocar de faixa e fechar não podem ficar presos em leitura bloqueante, nem em disco montado por rede.
- `core/audio/ring` — SPSC lock-free.
- `core/audio/seqlock` — publicação de estado sem lock no lado do áudio.
- `core/audio/engine` — callback, máquina de estados, posição real em frames.
- Play, pause, stop, seek, volume bruto. Abertura por `argv`.
- Log assíncrono com redação de credenciais.

Verificação: AU-02..AU-07, AU-09, AU-20; PL-01, PL-02, PL-03, PL-21, PL-22; AR-02, AR-04, AR-09; IN-07.

Limites medidos:
- Cancelamento de abertura bloqueante retorna em **< 100 ms**.
- Seqlock sob escrita concorrente: leitor **nunca** observa campos de gerações diferentes.

Marco de risco: a integração libav + miniaudio já foi validada no M0 pelo `pang_probe`. O que resta de risco aqui é o caminho de tempo real.

---

### M2 — Playlist e metadados

Entrega: playlist funcional, interface ainda provisória.

- `core/playlist/model`, `shuffle`, `m3u`.
- `core/meta/tags` assíncrono no pool de trabalho.
- Anterior, próxima, shuffle com histórico, três modos de repeat.
- Varredura recursiva, drag-and-drop, import/export M3U/M3U8/PLS.
- Lista virtualizada.

Verificação: PL-04..PL-07, PL-11, PL-12, PL-24, PL-25, PL-26; LI-01..LI-17; MD-01, MD-02; AR-03; RB-01..RB-03, RB-06.

Limite medido: 10 000 itens carregados e ordenados sem bloquear o thread de UI por mais de **50 ms** em nenhum ponto.

---

### M3 — Processamento de áudio

Entrega: equalizador, ganhos, limitador e gapless funcionando e medidos.

- `core/dsp/biquad`, `equalizer`, `gain`, `limiter`.
- Preamp, 10 bandas com a tabela de Q de `ARCHITECTURE.md §7`, shelving nas extremidades.
- Bypass com crossfade, abrangendo o preamp.
- Balanço, volume com rampa, ReplayGain.
- **Limitador com lookahead de 1,5 ms + clamp rígido**, latência somada ao número reportado.
- `core/audio/gapless` com drenagem explícita do `swr` e invalidação por geração.

Verificação: EQ-01..EQ-05, EQ-08, EQ-09, EQ-11, EQ-14; PL-09, PL-10; AU-10, AU-13, AU-14, AU-15, AU-16, AU-21, AU-22.

Limites medidos:
- Resposta do EQ por banda, medida uma banda por vez, dentro de **±1 dB**.
- Bypass após o crossfade: saída do estágio **bit a bit idêntica** à entrada, preamp incluído.
- Preamp +12 dB sobre sinal a 0 dBFS: **nenhuma amostra** acima de **-1.0 dBFS**.
- Transiente de uma amostra a 0 dBFS: contido pelo lookahead, **contador do clamp rígido = 0**.
- **Gapless**: rampa de contador cortada em dois arquivos, tocada em sequência → saída contém a sequência original **sem nenhuma amostra a mais nem a menos** na junção. WAV primeiro (mecanismo de emenda), depois MP3/Opus/Vorbis/AAC (delay e padding).
- Callback de áudio: **zero** alocações, verificado por interposição de `malloc`.

---

### M4 — Visualizações

Entrega: espectro e osciloscópio derivados do sinal real.

- `core/dsp/fft` — radix-2 real, Hann, truque L/R em uma transformada.
- `core/dsp/analyzer` — 19 barras log, escala dB, suavização, picos.
- **Ring de captura com descarte contado**, sem sobrescrita, e captura desligável na origem.

Verificação: VI-01..VI-21.

Limites medidos:
- Senoide 1 kHz @ 44.1 kHz: pico no bin **23 ±1**, amplitude **0 dBFS ±0.5 dB**.
- Silêncio: nenhum `NaN`/`inf`, tudo no piso de -70 dBFS.
- Anti-fase: energia total **dentro de 3 dB** da do canal isolado.
- Ring cheio: bloco descartado, contador incrementado, **nenhum quadro rasgado** sob leitura concorrente.
- Captura desligada: **nenhuma cópia** executada no callback.

---

### M5 — Aparência definitiva

Entrega: **o player com cara de Winamp.**

- Atlas de sprites redesenhado (trabalho de arte — risco #1).
- `ui/skin/atlas`, `bitmapfont`.
- **Modo integrado primeiro**: três painéis em uma janela, que é o modo garantido em todas as plataformas, inclusive Wayland.
- Modo destacado em seguida, com encaixe onde a plataforma permite e opção desabilitada com explicação onde não permite.
- Display completo; escala inteira; recuperação de janela pela regra da faixa de arraste.

Verificação: AP-01..AP-18; PL-13..PL-20; EQ-07, EQ-12; LI-04, LI-05, LI-10; AU-17; IN-09..IN-11.

---

### M6 — Integração com o sistema e streaming

Entrega: player integrado ao desktop nos três sistemas, tocando rádio.

- Streaming HTTP/HTTPS, buffering, ICY, redirecionamento, reconexão limitada.
- Seek desabilitado em fonte não pesquisável; duração indefinida em stream ao vivo.
- Seleção de dispositivo, recuperação de desconexão.
- **MPRIS (Linux), SMTC (Windows), MPNowPlayingInfoCenter (macOS)** — um arquivo por sistema em `platform/`.
- Teclas de mídia, always-on-top, atalhos, acessibilidade, instância única.
- Janela de propriedades do arquivo.

Verificação: PL-08, PL-23; AU-11, AU-12; MD-03..MD-09; IN-01..IN-08; AR-06; RB-04, RB-05.

**Entregue em três partes**, porque as naturezas de verificação são diferentes:

| Parte | Escopo | Como é verificado |
|---|---|---|
| M6-1 | Streaming, ICY, redirecionamento, reconexão contada, seek e duração em fonte ao vivo | `tests/test_stream.cpp` sobe um servidor HTTP local com quatro comportamentos |
| M6-2 | Dispositivo de saída e recuperação de perda | Política pura em `test_device.cpp`; perda real criando e removendo um sink com `pactl` em `test_device_loss.cpp` |
| M6-3 | MPRIS, teclas de mídia, sempre no topo, instância única, propriedades, tooltips, atalhos | `test_mpris.py` e `test_single_instance.py` falam com o player de verdade pelo barramento |

Windows (SMTC) e macOS (MPNowPlayingInfoCenter) usam `integration_none.cpp` até serem implementados: o player roda sem aparecer no painel do sistema, que é degradação prevista e não falha.

---

### M7 — Empacotamento Linux e relatório

- AppImage e/ou Flatpak, **com FFmpeg LGPL** ou decisão explícita por GPL-3.0 (risco #6).
- Matriz de formatos efetivamente disponíveis no pacote.
- `DEPENDENCIES.md` final, `TEST_REPORT.md`, `APROXIMACOES.md`, `LIMITACOES.md`.
- Teste de execução prolongada.

Verificação: AU-08; RB-07; EN-01..EN-08.

Limites medidos (definidos **antes** da execução, em máquina registrada no relatório):
- 8 h contínuas com **zero** xruns registrados pelo backend.
- CPU média **< 3 %** de um núcleo, com visualização ligada e EQ ativo.
- Crescimento de memória residente **< 5 MB** nas 8 h.

---

### M8 — Empacotamento Windows

- `windeployqt`, FFmpeg shared via vcpkg, instalador NSIS.
- Teclas de mídia e SMTC verificados em máquina real.

Verificação: EN-09; AR-08 (núcleo único aprovado nos testes dos três sistemas).

---

### M9 — Empacotamento macOS

- Bundle `.app`, `macdeployqt`, FFmpeg via vcpkg ou build próprio.
- **Assinatura e notarização** — exige conta paga de desenvolvedor Apple. Decisão pendente (risco #12).

Verificação: EN-10; AR-08.

---

## Observações e riscos

**1. O atlas de sprites é o caminho crítico do M5, e não é código.**
Desenhar botões, sliders, mostradores, fonte bitmap e os cinco estados de cada controle é trabalho de arte com prazo próprio. Deve começar em paralelo a partir do M2. Se atrasar, o M5 atrasa inteiro e não há como comprimir com mais programação.

**2. A janela clássica tem 275×116 px.**
Em 4K é um selo postal. A escala inteira resolve, mas 2× e 3× são os modos realmente usáveis; 1× é caso de teste, não modo padrão esperado. Escala fracionária foi descartada.

**3. A curva exata do equalizador do Winamp nunca foi publicada.**
As dez frequências são conhecidas, o Q e o tipo de filtro não. A versão 0.2 fixa fórmula, tabela de valores e shelving nas extremidades (`ARCHITECTURE.md §7`) — "estreitado proporcionalmente" não era implementável. O limite de Q em 4.0 causa sobreposição entre as bandas agudas; é comportamento normal de equalizador gráfico e está declarado.

**4. Gapless é uma promessa por formato — e a medição corrigiu a previsão.**
O risco original dizia que MP3 seria o caso frágil e Ogg Vorbis o caso garantido. Medido no M3, é o inverso: WAV, FLAC, MP3 e AAC fecham exatos; Opus erra 1 quadro; **Ogg Vorbis perde 256 quadros (5,8 ms)** ao ser cortado em fronteira de página. A tabela está em `ARCHITECTURE.md §5`. O que o arquivo não contém nenhum player recupera — a limitação é do material, não da emenda.

**5. Disponibilidade de formato é propriedade do pacote, não do código.**
Um AppImage com FFmpeg completo toca os seis; um Flatpak sobre runtime enxuto pode não ter AAC. AU-08 exige medir no artefato distribuído.

**6. FFmpeg LGPL: o build importa. Confirmado no M0.**
O FFmpeg desta máquina é `--enable-gpl --enable-version3`. Distribuir vinculado a ele torna o produto GPL-3.0. Decisão pendente: compilar FFmpeg LGPL próprio, ou adotar GPL-3.0 conscientemente. Não bloqueia M1–M6.

**7. Wayland impede o encaixe entre janelas destacadas.**
`xdg-shell` não expõe posição absoluta nem aceita posicionamento pelo cliente, e o Qt não contorna restrição de compositor. Como o desenvolvimento acontece em Hyprland, a promessa da versão 0.1 estava quebrada na própria máquina de origem. Por isso o **modo integrado**, com três painéis em uma janela, passa a ser o modo padrão e garantido, e o destacado vira melhor esforço com matriz por plataforma (`ARCHITECTURE.md §8`). Isso também simplifica o M5: o caminho garantido é o mais simples de implementar.

**8. O callback de áudio não pode esperar por nada — nem pela visualização.**
Daí o descarte contado no ring de captura e o seqlock no lugar de `std::atomic<struct>`. Ambos são correções da versão 0.1, que presumia sobrescrita segura e atomicidade lock-free onde não havia.

**9. Latência passa a ser um número público.**
O lookahead de 1,5 ms do limitador é o preço de garantir o teto de saída. A interface reporta latência total (lookahead + buffer do dispositivo), para que o compromisso seja visível em vez de implícito.

**10. Onde se tomam as decisões caras.**
`core/` não conhecer Qt significa que toda a seção 11 da especificação — a parte de DSP, a mais difícil de verificar visualmente — roda headless em CI, **nos três sistemas**. É esse o benefício concreto da camada, não pureza arquitetural.

**11. Não há repositório git nem CI ainda.**
A matriz de três sistemas do M0 precisa de `git init` e de runners. Enquanto isso não existir, "M0 verde nos três sistemas" é uma intenção, não um fato — e o `TEST_REPORT.md` registra assim.

**12. macOS acrescenta um custo que não é técnico.**
Distribuir fora da App Store exige assinatura e notarização, o que exige conta paga de desenvolvedor Apple. Sem isso, o `.app` roda apenas com aviso de segurança contornável pelo usuário. Decisão necessária antes do M9; não bloqueia nada antes disso.

**13. Ainda não decidido, e pode esperar.**
Formato do arquivo de coordenadas do atlas; AppImage vs. Flatpak vs. ambos; se o modo compacto é um estado do modo integrado ou um layout próprio. Nenhum bloqueia o M1.

---

## Ordem de ataque

M1 é sequencial e urgente. O trabalho de arte do atlas corre em paralelo a partir do M2. M3 e M4 podem ser desenvolvidos em paralelo entre si — compartilham apenas o ponto de captura, já definido. M5 depende do atlas. M6 depende de M5. M7, M8 e M9 são independentes entre si e só precisam de `core/` estável.
