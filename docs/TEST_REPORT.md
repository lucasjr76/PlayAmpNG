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

---

## M0 — revisão 0.2 (após crítica técnica)

Data: 2026-09-18

### Executado

| Verificação | Tipo | Resultado |
|---|---|---|
| `NDEBUG` está mesmo presente na configuração padrão | MANUAL | **Confirmado** — `-DNDEBUG` aparece nas flags reais de `RelWithDebInfo`. `assert()` estaria morto silenciosamente; AR-10 não era teórico |
| `PANG_CHECK` avalia a condição sob `NDEBUG` | AUTO | **Passou** — teste `core`, caso `test_check_survives_ndebug` |
| `PANG_CHECK` relata falha e sai com código ≠ 0 | MANUAL | **Passou** — falha proposital compilada com `-DNDEBUG -O2` imprimiu a condição e retornou `exit=1` |
| Suíte `core` roda headless, sem Qt e sem dispositivo | AUTO | **Passou** — 5 casos, 2/2 testes CTest verdes |
| Enumeração tolera ausência de dispositivos | AUTO | **Passou** — critério é "lista OU erro, nunca os dois" |
| Link específico por plataforma corrigido | MANUAL | `libm` deixou de ser ligado no Windows; frameworks CoreAudio/AudioToolbox adicionados no macOS. **Não verificado em execução** — ver abaixo |

### Não verificado

- **Build e testes em Windows e macOS.** A matriz de CI existe (`.github/workflows/ci.yml`), o repositório foi inicializado, mas nada rodou ainda: não há remoto nem execução de workflow. O requisito de "M0 verde nos três sistemas" permanece **NÃO VERIFICADO**, não `OK`. A perna do Windows (Qt por action + FFmpeg por vcpkg) é a mais provável de precisar de ajuste na primeira execução.
- Reprodução de áudio, FLAC, Vorbis e AAC, DSP, playlist, interface e integração com o sistema.

### Requisitos atendidos

`EQ-13` e `AR-10` → `OK (M0)`, somando-se aos oito da revisão 0.1. Total: 10 de 175.

### Correções de documento nesta revisão

Doze itens, listados em `ARCHITECTURE.md`, seção "Mudanças da versão 0.1 para a 0.2". Os quatro de maior consequência técnica: limitador sem lookahead não podia cumprir o teto prometido; o ring de visualização tinha corrida de leitura; o "bloco de estado atômico" presumia lock-free onde não há; e o critério de gapless podia passar por acidente.

---

## M1 — Reprodução real

Data: 2026-09-18 · Máquina: ver `DEPENDENCIES.md`

Suíte: `ctest` 3/3 verdes (`core`, `audio`, `m0_headless`), 0,64 s.

### Decodificação e reprodução (AU-02 a AU-07)

Cada formato foi verificado por dois caminhos independentes: `Engine::render()` chamado diretamente pelos testes (headless, sem dispositivo) e reprodução pelo dispositivo de áudio real via `pang_probe --play`.

| Formato | Headless (RMS / duração) | Dispositivo real | Underruns |
|---|---|---|---|
| WAV | **Passou** | 0,500 s | 0 |
| MP3 | **Passou** | 0,500 s | 0 |
| FLAC | **Passou** | 0,500 s | 0 |
| Ogg Vorbis | **Passou** | 0,497 s | 0 |
| Opus | **Passou** | 0,500 s | 0 |
| AAC/M4A | **Passou** | 0,500 s | 0 |

Sinal de teste: senoide de 1 kHz, amplitude 0,5 exata (−6,0 dBFS de pico, −9,0 dBFS RMS confirmados por `volumedetect`), 0,5 s, estéreo, 44100 Hz. O teste exige RMS entre 0,32 e 0,39 — silêncio, ruído ou erro de ganho reprovam.

MP3 e AAC chegando a 0,500 s exatos indica que o corte de delay e padding do libavcodec está operando, o que é a premissa do gapless do M3. Ogg Vorbis fica 3 ms curto, atribuível ao corte por granule position; registrado para reexame em AU-14.

### Demais verificações

| Verificação | Tipo | Resultado |
|---|---|---|
| PL-22 — seek posiciona no alvo | AUTO | **Passou** — tolerância 100 ms; resta ~0,25 s após buscar a metade |
| PL-02 — pausar não consome nem avança, e entrega silêncio | AUTO | **Passou** |
| PL-02 — retomar continua do ponto de pausa | AUTO | **Passou** |
| PL-03 — parar zera a posição e mantém a faixa | AUTO | **Passou** |
| AR-02/AR-04 — em `Error` o render entrega silêncio | AUTO | **Passou** |
| RB-01 — arquivo ausente leva a `Error` com mensagem | AUTO | **Passou** |
| AR-11 — seqlock sob escrita concorrente | AUTO | **Passou** — ~1,7 M leituras, zero blocos rasgados |
| FloatRing — volta ao início e escrita em buffer cheio | AUTO | **Passou** |
| AR-06 — redação de credenciais em URL | AUTO | **Passou** — senha e token removidos, host e parâmetros inócuos preservados |
| IN-07 — abertura por argumento de linha de comando | MANUAL | **Passou** — `playampng assets/test/tone.wav` |

### Defeitos encontrados e corrigidos durante o M1

1. **`Engine::seek()` não publicava a posição.** A posição ia para o atômico interno, mas o `Snapshot` lê do bloco publicado pelo seqlock, que só é escrito por `render()`. Com o áudio suspenso durante a busca — que é a precondição do método — a interface continuava mostrando a posição antiga. Corrigido publicando no próprio `seek()`. **O teste pegou; a inspeção não teria.**

2. **`Seqlock` trocado por palavras atômicas.** A primeira versão guardava o conteúdo como um `T` comum copiado com `memcpy`, o que é corrida de dados formal — comportamento indefinido que o otimizador pode explorar. Substituído por um vetor de `std::atomic<uint64_t>` com ordem `relaxed`, mantendo as fences. Mesmo custo, sem UB.

3. **Dois falsos positivos nos meus próprios testes**, ambos diagnosticados com medição em vez de suposição:
   - O RMS esperado era 0,707, mas o arquivo gerado pelo filtro `sine` do FFmpeg estava a −21 dBFS, não em escala plena. O decodificador estava certo; a expectativa é que estava errada. Os arquivos foram regerados com `aevalsrc` e amplitude explícita de 0,5.
   - O teste do seqlock contava como "bloco rasgado" as leituras anteriores à primeira publicação, quando o conteúdo ainda é o valor inicial. Adicionado `versions()` para o teste esperar a primeira publicação.

### Não verificado

- **AR-09 — cancelamento de abertura de rede bloqueada.** O `AVIOInterruptCB` está instalado e `rw_timeout` configurado, e o teste confirma que parar durante a decodificação retorna em menos de 500 ms. Mas isso exercita o cancelamento da decodificação, não o de uma abertura de socket travada, que é o caso que motiva o requisito. Fica **PARCIAL** até o M6, quando houver streaming para travar de propósito.
- **AR-06** fica PARCIAL pelo mesmo motivo: a função de redação está verificada, a integração com URLs de streaming chega no M6.
- **AU-10** (rampa de volume) está implementada mas ainda não medida — a medição é do M3.
- RB-02 e RB-03 (arquivo corrompido, sem permissão), Windows e macOS.

### Requisitos atendidos

19 em `OK (M1)`, 2 em `PARCIAL (M1)`. Total acumulado: 29 de 175 com estado diferente de `PENDENTE`.

---

## M2 — Playlist e metadados

Data: 2026-09-18

Suíte: `ctest` 4/4 verdes (`core`, `audio`, `playlist`, `m0_headless`), ~5,2 s. Executada três vezes seguidas sem variação.

### Executado

| Verificação | Tipo | Resultado |
|---|---|---|
| LI-06/LI-07 — remover e limpar sem tocar nos arquivos | AUTO | **Passou** |
| LI-04 — reordenar, com o id acompanhando o item | AUTO | **Passou** |
| LI-08 — ordenar por caminho, artista e duração; campo ausente vai para o fim | AUTO | **Passou** |
| LI-09 — busca textual sem diferenciar caixa | AUTO | **Passou** |
| LI-11 — total soma só as durações conhecidas, e conta as ausentes à parte | AUTO | **Passou** |
| MD-02 — sem tag de título, usa o nome do arquivo | AUTO | **Passou** |
| LI-03 — varredura recursiva e não recursiva, ordem determinista | AUTO | **Passou** |
| LI-12 — round-trip M3U8 e PLS preserva ordem, caminhos e duração desconhecida | AUTO | **Passou** |
| LI-13 — caminho relativo resolvido contra o diretório da playlist | AUTO | **Passou** — arquivo resolvido existe em disco |
| PLS com chaves fora de ordem | AUTO | **Passou** — o índice da chave manda, não a ordem das linhas |
| LI-15 — ciclo de shuffle cobre todos sem repetir | AUTO | **Passou** — 10 itens, 10 distintos |
| LI-14 — "anterior" percorre o histórico real, na ordem inversa da ida | AUTO | **Passou** |
| PL-04 — anterior vai à faixa anterior; na primeira, permanece nela | AUTO | **Passou** |
| PL-25 — trocar de faixa durante a pausa continua pausado, posição 0 | AUTO | **Passou** |
| PL-24 — fim da playlist: `off` para, `all` volta à primeira, `track` repete | AUTO | **Passou** |
| AR-03 — resultado de metadados com id inexistente é descartado | AUTO | **Passou** |
| LI-16 — leitura de tags em segundo plano, resultado carimbado com id | AUTO | **Passou** |
| RB-02 — MP3 corrompido termina em `Error`, sem travar nem tocar lixo | AUTO | **Passou** |
| RB-03 — varredura pula diretório sem permissão e continua | AUTO | **Passou** |
| LI-02 — drag-and-drop de arquivos e pastas | MANUAL | **Passou** |

### LI-17 e RB-06 — medição com 10 000 itens

```
inserir 1 ms · ordenar 8 ms · buscar 0 ms · aplicar metadados 13 ms
```

Limite definido antes da execução: nenhuma operação acima de 50 ms no thread da interface. Todas ficaram uma ordem de grandeza abaixo.

A aplicação de metadados exercita `Playlist::index_of()`, que é busca linear — aplicar em 10 000 itens é O(n²). Medido: 13 ms. O comentário `ponytail:` no código registra o teto e o caminho de substituição, mas **não há motivo para trocar**: o custo real é irrelevante nesta escala. Otimizar aqui seria trabalho sem retorno.

A lista usa `QListView` sobre `QAbstractListModel`, e não `QListWidget`: só as linhas visíveis são consultadas.

### Defeito de teste encontrado — e a lição

Dois testes de fim de playlist estavam escritos como "rode 1,5 s e olhe o índice". Com faixas de 0,5 s, isso cai ora depois de uma troca, ora depois de duas. O caso `repeat=track` falhou de imediato e foi corrigido; o caso `repeat=all` **passou na primeira execução e falhou nas quatro seguintes**.

```
execucao 1:
execucao 2: 349: fim da playlist com repeat=all volta para a primeira
execucao 3: 349: fim da playlist com repeat=all volta para a primeira
```

Corrigido com `run_until_next_load()`, que roda até a geração do engine mudar — a geração é monotônica e não depende de onde o relógio parou. O caso `repeat=off` inverte o mesmo instrumento: passa justamente quando o prazo estoura sem troca alguma.

Um teste que passa por acidente é pior que um teste ausente, porque dá confiança falsa. A suíte foi executada seis vezes seguidas depois da correção, sem variação.

### Não verificado

Interface definitiva (M5), streaming (M6), Windows e macOS. `LI-05` (seleção múltipla) e `LI-10` (destaque da faixa em reprodução) estão implementados e conferidos a olho na interface provisória; a verificação formal é do M5, junto com o restante da aparência.

### Requisitos atendidos

31 em `OK (M2)`. Total acumulado: 60 de 175 com estado diferente de `PENDENTE`.

---

## M3 — Processamento de áudio

Data: 2026-09-18

Suíte: `ctest` 7/7 verdes (`core`, `audio`, `dsp`, `realtime`, `gapless`, `playlist`, `m0_headless`), ~4,2 s.

### EQ-03 — resposta do equalizador, banda por banda

Medida com senoide na frequência de medição, comparando RMS de saída e de entrada. Limite definido antes: **±1 dB**.

| Banda | Pedido −12 dB | −6 dB | +6 dB | +12 dB |
|---|---|---|---|---|
| 170 Hz a 14 kHz (peaking) | −12,00 | −6,00 | +6,00 | +12,00 |
| 60 Hz (low-shelf, medido a 20 Hz) | −11,17 | −5,65 | +5,65 | +11,17 |
| 16 kHz (high-shelf, medido a 20 kHz) | −11,24 | −5,68 | +5,68 | +11,24 |

As oito bandas peaking acertam o ganho pedido **exatamente**. As duas shelving ficam a 0,83 dB do pedido, dentro do limite — e a razão é estrutural, não defeito: num shelf RBJ o ganho pedido é o do platô, e com slope 0,7 o platô do shelf de 16 kHz só se completa acima de 25 kHz, além de Nyquist a 44,1 kHz. Medir em f0 daria metade do ganho e reprovaria por engano; isso está anotado no teste.

### Q por banda (EQ-13)

```
1.18  1.56  1.68  1.21  1.08  1.41  2.34  4.00
```

Bate com a tabela de `ARCHITECTURE.md §7`. O valor de 14 kHz é o teto de 4,00, e não os 6,94 que a fórmula produziria.

### EQ-04, EQ-14, EQ-15 — bypass

| Verificação | Resultado |
|---|---|
| Saída bit a bit igual à entrada após o crossfade, preamp incluído | **Passou** |
| Maior salto por amostra: controle 0,3441 · durante a transição 0,3354 | **Passou** — a transição não introduz salto maior que o do próprio sinal |
| Pico da cauda após o bypass: 0,3000, com entrada 0,3 | **Passou** |

### AU-16, AU-22, AU-23 — limitador

| Medição | Resultado |
|---|---|
| Senoide a +12 dBFS → pico de saída | **0,8913 = −1,00 dBFS**, exatamente o teto |
| Atuações do clamp rígido | **0** |
| Transiente de uma amostra a 0 dBFS em silêncio → pico | 0,8913, clamp **0** |
| Latência | 66 quadros = **1,50 ms** |
| Sinal abaixo do teto | transparente (< 0,05 dB) |

O clamp rígido em zero é o resultado que importa: o lookahead conteve tudo sozinho, e a rede de segurança nunca precisou entrar. Um valor diferente de zero aqui seria falha.

### PL-09, PL-10, AU-10 — ganhos

| Medição | Resultado |
|---|---|
| Volume 0,5 | **−6,02 dB** |
| Canais no centro | dentro de 0,1 dB um do outro |
| Balanço extremo | canal oposto silenciado; o escolhido **não** é amplificado |
| Maior salto por amostra na rampa de volume | 0,000567, limite 0,001134 |

### AU-13 — gapless, contagem exata de amostras

```
esperadas 88200 amostras, produzidas 89088, divergentes 0
```

Sinal de rampa cortado em dois arquivos, tocado com emenda, comparado amostra a amostra com a concatenação do original. **Nenhuma divergência**: a emenda não insere nem remove uma única amostra. O alinhamento desconta o atraso do limitador de propósito, sem correlação cruzada — correlação mascararia justamente o defeito procurado.

Há contraprova: sem informar a próxima faixa, a reprodução para no fim da primeira. Sem ela, o teste principal poderia estar medindo nada.

### AU-14 — disponibilidade por formato, e uma previsão errada

Soma das partes contra o original de 44100 quadros:

```
WAV          24576 + 19524 = 44100  (+0 quadros)
FLAC         24576 + 19524 = 44100  (+0 quadros)
MP3          24576 + 19524 = 44100  (+0 quadros)
AAC/M4A      24576 + 19524 = 44100  (+0 quadros)
Opus         24576 + 19525 = 44101  (+1 quadro, +0,0 ms)
Ogg Vorbis   24448 + 19396 = 43844  (-256 quadros, -5,8 ms)
```

**Isso contradiz o que eu havia documentado.** O risco #4 do plano dizia que MP3 seria o caso frágil, por depender da tag LAME/Xing, e que Ogg Vorbis funcionaria sempre. É o inverso: o LAME grava a tag por padrão e o libavcodec a aplica, então MP3 fecha exato; o Ogg Vorbis cortado em fronteira de página perde 256 quadros no próprio material. `ARCHITECTURE.md §5` e `PLAN.md` foram corrigidos.

Esse desvio explica a observação solta do M1, quando o tom de 0,5 s em Ogg reproduziu 0,497 s.

### AU-21 — o callback não aloca

```
alocacoes em 200 chamadas de render(): 0
```

Verificado por interposição de `operator new` num binário de teste próprio, exercitando também mudança de banda, preamp, volume, balanço e ativação de bypass durante a reprodução — que é onde uma alocação descuidada costuma se esconder. Limite conhecido: a interposição pega `operator new`, não `malloc` cru; serve porque `render()` só executa código nosso, com o libav do outro lado do ring.

### Defeitos encontrados e corrigidos

1. **Bypass só valia para o primeiro sub-bloco.** Quando o crossfade terminava no meio de uma chamada de `process()`, o retorno antecipado protegia apenas o início da chamada: os sub-blocos seguintes voltavam a sair processados. Com entrada de amplitude 0,3, a cauda media **2,38**. O teste pegou; a inspeção não teria.

2. **A geração subia na decodificação, não na reprodução.** Com gapless, o decodificador abre a faixa seguinte enquanto ainda há ~2 s de áudio da anterior no buffer. Publicar a geração ali fazia a interface trocar de faixa segundos antes de o som trocar. Separado em `generation_seq_` (atribuição) e `generation_` (publicação, no instante em que o áudio da nova faixa alcança a saída).

3. **Um teste meu com premissa errada.** A transição de bypass era medida contra um limite absoluto de 0,2, calculado sobre a amplitude de entrada — mas com dez bandas em +12 dB o sinal processado fica várias vezes maior, e o declive da própria senoide já produz saltos de 0,34. O limite passou a sair do sinal: a mesma passagem sem trocar o bypass serve de controle.

### Não verificado

- **EQ-07** (criar, editar, salvar e excluir presets do usuário) está implementado e conferido a olho; a verificação formal é do M5.
- **IN-09/IN-10** (gravação atômica e recuperação de configuração inválida) estão implementados com `QSaveFile` e renomeação para `.bad`; verificação formal no M5.
- Detecção de true peak: **não implementada e não prometida**, por decisão registrada em `ARCHITECTURE.md §5`.
- Windows e macOS.

### Requisitos atendidos

23 em `OK (M3)`. Total acumulado: 83 de 175 com estado diferente de `PENDENTE`.
