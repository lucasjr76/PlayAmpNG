# PlayAmpNG — Documento de Arquitetura

Versão 0.2 — revisão após crítica técnica do M0. Mudanças em relação à 0.1 estão listadas no fim.

Alvos: **Linux, Windows e macOS**. Linux é o ambiente principal de desenvolvimento; os três são construídos e testados desde o M0.

---

## 1. Stack escolhida e justificativa

| Camada | Escolha | Justificativa |
|---|---|---|
| Linguagem | C++20 | Callback de áudio em tempo real exige ausência de GC, de GIL e de alocação implícita. Bibliotecas de áudio e de decodificação são C/C++ nativas. |
| Framework gráfico | Qt 6 (Widgets) | Usado **apenas** como camada de janela/evento/diálogo. Fornece prontos: DPI, drag-and-drop, diálogos de arquivo, foco e navegação por teclado, nomes acessíveis, QtDBus, `QSaveFile` (gravação atômica), `QLocalServer` (instância única), `QStandardPaths`. |
| Desenho da interface | Sprite atlas + `QPainter` | O visual do Winamp Classic é pixel art blitado, não uma árvore de widgets temáveis. |
| Decodificação / demux | FFmpeg (libavformat, libavcodec, libswresample) | Uma dependência cobre os seis formatos, mais HTTP/HTTPS, redirecionamentos, ICY, seek e tags ReplayGain. Build LGPL-2.1 obrigatório para distribuição (ver `DEPENDENCIES.md`). |
| Saída de áudio | miniaudio | Abstrai PipeWire/PulseAudio/ALSA, WASAPI e CoreAudio, com enumeração de dispositivos, notificação de troca e float32 nativo. |
| DSP | Código próprio | Biquads RBJ, FFT e limitador são algoritmos pequenos e bem definidos. |
| Persistência | JSON + `QSaveFile` | Inspecionável, diffável, reparável à mão. |
| Testes | CTest + `PANG_CHECK` | `core/` não depende de Qt nem de dispositivo, então roda headless em CI nos três sistemas. |

### O que foi deliberadamente recusado

- **Suporte a skins / parser `.wsz`** — proibido pela seção 2 da especificação, e os bitmaps originais são material protegido.
- **Codecs próprios** — proibido pela seção 4.
- **Scanner de ReplayGain** — lemos as tags; calcular ganho é extensão de escopo.
- **Abstrações com uma implementação** — onde a variação por sistema é real, quem escolhe o arquivo é o CMake.

---

## 2. Camadas

> `core/` não inclui Qt, não inclui cabeçalho de sistema operacional e não contém `#ifdef` de plataforma. A regra é imposta pelo build (AR-07), a cada compilação.

```
src/
  core/                    C++20 puro. Sem Qt. Sem API de OS. Testável headless.
    audio/
      probe.{h,cpp}        propriedades da fonte; describe() é o ponto único
      decoder.{h,cpp}      libav: abrir, decodificar, reamostrar, buscar
      network.{h,cpp}      opções de abertura de fonte remota, lista de protocolos
      engine.{h,cpp}       ring, gapless por segmentos, estados, reconexão
      ring.h               SPSC lock-free
      seqlock.h            publicação do thread de áudio, sem corrida
    dsp/
      biquad.{h,cpp}       coeficientes RBJ
      equalizer.{h,cpp}    dez bandas + preamp, shelving nas pontas
      gain.{h,cpp}         volume, balanço, ReplayGain
      limiter.{h,cpp}      lookahead + clamp rígido
      fft.{h,cpp}          radix-2, janela de Hann
      analyzer.{h,cpp}     barras, escala log, suavização, picos
      presets.{h,cpp}      presets integrados e do usuário
    playlist/              playlist, shuffle, m3u/pls, track
    meta/                  tags, scanner assíncrono
    state/                 player (estados e snapshot), controller
    util/                  log com redação de credenciais, PANG_CHECK

  platform/                ÚNICO lugar onde o OS aparece. Arquivo escolhido pelo CMake.
    audio_device.{h,cpp}   miniaudio: enumerar, abrir, detectar parada, reabrir
    device_recovery.h      política de reabertura — lógica pura, testável sem placa
    integration.h          interface: now-playing, teclas de mídia
    integration_linux.cpp  MPRIS (QtDBus)
    integration_none.cpp   sistemas sem integração ainda (Windows, macOS)
    miniaudio_impl.c       unidade de tradução do miniaudio

  ui/                      Qt 6. Só apresentação e entrada.
    skin/
      winamp_layout.h      a GRADE: margem 14, vão ≥ 3, e a geometria do formato
      winamp_skin.{h,cpp}  carrega .wsz ou pasta; sprites, fonte, recorte por cor
      zip.{h,cpp}          leitor de ZIP sobre zlib, só o que o .wsz usa
    panel/                 painéis: player, equalizador, playlist
    shell/
      integrated.{h,cpp}   três painéis em UMA janela — modo garantido
      recovery.{h,cpp}     recuperação de janela fora da área visível
    settings.{h,cpp}       gravação atômica; identidade da instância sai daqui
    single_instance.{h,cpp} IN-08, sobre QLocalServer
    app.cpp                argv, instância única, ciclo de vida

packaging/linux/           .desktop, metainfo, ícones, AppImage e Flatpak
assets/skin/               PNGs do atlas + JSON de coordenadas
tests/                     testes de core/, sem áudio e sem janela
```

### Superfície real dependente de sistema

| Necessidade | Linux | Windows | macOS | Duplicado? |
|---|---|---|---|---|
| Saída de áudio, dispositivos, desconexão | miniaudio | miniaudio | miniaudio | Não |
| Caminhos, gravação atômica, diálogos, DPI, teclado | Qt | Qt | Qt | Não |
| Instância única | `QLocalServer` | `QLocalServer` | `QLocalServer` | Não |
| Now-playing + teclas de mídia | MPRIS | SMTC | MPNowPlayingInfoCenter | **Sim — 1 arquivo por sistema** |
| Posicionamento de janelas destacadas | **restrito no Wayland** | ok | ok | Ver §8 |
| Empacotamento | AppImage/Flatpak | NSIS | .app + notarização | Build, não código |

### Critério de portabilidade (AR-08, revisado)

A formulação anterior — "o port não pode alterar nenhuma linha de `core/`" — era retoricamente satisfatória e errada como critério de aceitação: corrigir um bug de portabilidade no núcleo é manutenção normal, não falha de arquitetura. O critério correto:

> `core/` permanece **compartilhado e único**, sem dependência direta de API de sistema operacional (verificado por AR-07), e **passa na íntegra a suíte de testes nos três sistemas**.

Uma correção em `core/` é aceitável quando vale para os três alvos. O que a arquitetura proíbe é `core/` ganhar caminhos condicionais por sistema.

---

## 3. Modelo de threads

| Thread | Dono | Responsabilidade | Restrições |
|---|---|---|---|
| UI | Qt | Desenho, entrada, diálogos | Nunca bloqueia em I/O |
| Áudio | miniaudio | Cadeia DSP e preenchimento do buffer | **Sem alocação, sem lock, sem I/O, sem log síncrono, sem exceção, sem espera** |
| Decodificador | Nosso | libav: demux, decode, resample | I/O **com timeout e cancelamento** — ver abaixo |
| Pool de trabalho | `QThreadPool` | Varredura de diretório, tags, carga de playlist | Resultados carimbados com geração |

### Decodificador → Áudio

Ring SPSC lock-free de float32 intercalado, já na taxa e no layout do dispositivo. Pré-alocado uma vez.

### UI → Áudio

Fila de comandos lock-free de tamanho fixo, com structs POD (`SetVolume`, `SetEqBand`, `Seek`, `SetBypass`…). Nenhum ponteiro para objeto com destrutor, nenhuma alocação do lado do áudio.

### Áudio → UI: seqlock, não `std::atomic<struct>`

O bloco de estado publicado pelo áudio — posição em frames, estado, geração, pico, contador de atuação do clamp, contador de descartes da visualização — passa de 16 bytes. `std::atomic<T>` para um tipo desse tamanho é implementado **com lock** na maioria das plataformas, o que colocaria um mutex dentro do callback. Presumir lock-free aqui seria um erro silencioso.

O mecanismo é um **seqlock**:

- O áudio incrementa um contador para ímpar, escreve os campos, incrementa para par. **Nunca espera.**
- A UI lê o contador, lê os campos, relê o contador. Se mudou ou estava ímpar, tenta de novo. **Só o leitor gira.**

O único tipo cuja atomicidade é assumida é `std::uint64_t`, e a premissa fica registrada em código:

```cpp
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
```

Nenhum outro tipo é assumido lock-free sem o mesmo `static_assert`.

### Áudio → Visualização: descarte, nunca sobrescrita

A versão 0.1 dizia que o áudio sobrescreve blocos não consumidos. Isso é uma corrida: o consumidor pode estar lendo exatamente a região sobrescrita e produzir um quadro rasgado. O protocolo correto:

1. **Captura desligável na origem.** O callback só copia amostras se o átomo `capture_enabled` estiver ligado. Desligar a visualização desliga a captura — parar apenas o consumo deixaria o custo da cópia dentro do callback, e o requisito VI-17 fala em custo zero.
2. **Sem espaço, descarta.** Se não houver espaço livre para um bloco inteiro, o bloco novo é descartado. O callback nunca espera e **nunca escreve em região que o consumidor possa estar lendo**.
3. **Descarte é contado.** Cada descarte incrementa um contador no bloco de estado. Visualização perdendo quadros é diagnosticável, não invisível.
4. **Índice de leitura avança depois da cópia.** O consumidor copia o bloco para fora do ring antes de liberar o espaço.

Efeito: sob carga, a visualização atrasa ou pula quadros; o áudio nunca espera e nunca entrega dado rasgado.

### Decodificador: I/O com timeout e cancelamento

"Pode bloquear à vontade" é correto para disco local e falso para rede — e também para disco montado por rede. Todo `AVFormatContext` recebe:

- um **`AVIOInterruptCB`** que consulta um flag atômico de cancelamento a cada iteração bloqueante do libav;
- `rw_timeout` e os timeouts do protocolo nas opções de abertura.

Parar, trocar de faixa, trocar de estação ou fechar o aplicativo aciona o flag, e a operação bloqueante retorna com erro em vez de segurar o thread. Sem isso, fechar o player durante um stream travado espera o timeout do sistema operacional.

### A regra da geração

Toda carga de faixa recebe um `uint64 generation` monotônico. Toda resposta assíncrona — tags, duração, resultado de seek, erro de rede, **pré-carga de gapless** — carrega a geração de origem. Resultado com geração diferente da atual é descartado sem efeito.

---

## 4. Máquina de estados

```
                  load()          buffer pronto
   Stopped ─────────────────> Loading ──────────────> Playing
      ^                          │                    │   ^
      │                          │ falha              │   │
      │                          v              pause │   │ resume
      │                        Error                  v   │
      │                          │                  Paused │
      │  stop() de qualquer      │                    │    │
      └──────────────────────────┴────────────────────┴────┘

   Playing ──(stream esvaziou)──> Buffering ──(reabastecido)──> Playing
```

A interface reflete **o estado confirmado pelo engine**, nunca o estado otimista do clique.

### Semântica fixada dos casos ambíguos (spec §3)

| Situação | Comportamento |
|---|---|
| **Anterior** | Sempre vai para a faixa anterior, nunca reinicia a atual. Em shuffle, retrocede pelo histórico. Na primeira faixa com repeat=off, permanece nela. |
| **Parar** | `Stopped`, posição 0, faixa carregada e destacada, visualização a zero. |
| **Fim da playlist** | `off`: para na última, posição 0. `all`: segue para a primeira. `track`: repete a atual. |
| **Troca de faixa durante pausa** | Carrega na posição 0 e **permanece pausado**. |
| **Restauração de sessão** | `Stopped`, salvo preferência de autoplay. |

---

## 5. Cadeia de processamento de áudio

```
  libavformat  demux            (I/O cancelável, com timeout)
       │
  libavcodec   decode           (delay/padding JÁ removidos aqui — ver Gapless)
       │
  libswresample  resample + interleave  ──> f32, taxa e layout do DISPOSITIVO
       │
  [ring SPSC]               ← fim do thread decodificador
       │                       início do thread de áudio
  1. ganho ReplayGain       (track ou album, com rampa)
  2. preamp do equalizador
  3. cascata de 10 biquads  (estado independente por canal)
       │
       ├──────────────> [ring de visualização]   ◄── PONTO DE CAPTURA
       │
  4. balanço estéreo
  5. volume                 (rampa de 20 ms)
  6. limitador com lookahead de 1,5 ms
  7. clamp rígido em -1.0 dBFS
       │
  dispositivo (miniaudio, float32)
```

### Ponto de captura

Depois do equalizador, antes de balanço e volume. O espectro reage ao equalizador — que é o feedback esperado — e não encolhe quando o volume baixa, o que deixaria a visualização morta em volume zero. Aproximação documentada.

### Estratégia contra clipping

Objetivo mensurável: **nenhuma amostra na saída excede o teto.**

Um limitador com ataque de 5 ms e sem lookahead não entrega isso: durante o ataque, o pico já passou. *Soft-knee* descreve o formato da curva de ganho, não elimina o atraso de detecção. A versão 0.1 estava errada nesse ponto. O desenho corrigido:

1. **Headroom antes de tudo.** ReplayGain limitado ao teto configurado (padrão: apenas atenua).
2. **Limitador com lookahead de 1,5 ms** — 66 amostras a 44,1 kHz, buffer dimensionado pela taxa do dispositivo. O detector enxerga o pico antes de ele alcançar a saída, e o ganho já está reduzido quando ele chega. Release de 100 ms.
3. **Clamp rígido na última amostra.** É a garantia dura, bit a bit. Se o lookahead estiver correto, ele nunca atua. Cada atuação incrementa um contador — e um contador diferente de zero em teste é **falha**, não estatística.

**Latência.** Os 1,5 ms do lookahead entram na latência de saída e são somados à latência do dispositivo no número reportado pela interface. Para um player musical é um compromisso melhor que proteção incompleta: não há sincronia de vídeo nem monitoração ao vivo em jogo.

**Qual pico é limitado.** O teto é de **pico de amostra** (*sample peak*), em **-1.0 dBFS**.

Não é -0.3 dBFS porque pico de amostra não limita pico verdadeiro (*true peak*): a reconstrução no conversor pode ultrapassar a maior amostra em picos inter-amostra, tipicamente até ~0,5–1 dB em material denso. Um decibel de margem cobre a maioria dos casos sem o custo de detecção com sobreamostragem 4×.

O projeto **não promete** teto de true peak. Promete: nenhuma amostra acima de -1.0 dBFS na saída, verificável por inspeção do buffer. Detecção de true peak com sobreamostragem fica registrada como extensão possível, com custo declarado.

### Gapless

Definido em amostras, não em impressão auditiva.

**Quem aplica delay e padding.** O libavcodec, por padrão. O `AVPacket` carrega `AV_PKT_DATA_SKIP_SAMPLES` e o decodificador remove as amostras de início e de fim antes de entregar o quadro. O projeto **não** reimplementa esse corte e **não** liga `AV_CODEC_FLAG2_SKIP_MANUAL` — é exatamente assim que se evita descarte duplicado. Os quadros que chegam à nossa camada já vêm aparados.

**Em que taxa as contagens estão.** Na taxa do **codec**, antes da reamostragem, porque o corte acontece dentro do libavcodec. Depois do `swr`, contagens de amostra da fonte não têm mais correspondência inteira com a saída, e nenhum código nosso depende disso.

**Drenar o reamostrador.** No fim da faixa, `swr_convert` é chamado com entrada nula repetidamente até devolver 0. Sem isso, o atraso interno do filtro engole o final da faixa — e o buraco resultante seria atribuído por engano ao mecanismo de emenda.

**Próxima faixa com taxa ou canais diferentes.** Não é caso especial: o `swr` de cada faixa é configurado para a taxa e o layout **do dispositivo**, então o ring é homogêneo e a emenda acontece em amostras já convertidas. Uma faixa a 48 kHz seguida de uma a 44,1 kHz produz o mesmo formato no ring.

**Invalidar a pré-carga.** A pré-carga carrega a geração do item que se pretendia tocar em seguida. Qualquer coisa que mude quem é o próximo — reordenar, remover, limpar, alternar shuffle ou repeat, pular manualmente — incrementa a geração. Na transição, pré-carga com geração diferente da esperada é descartada e a faixa é aberta pelo caminho normal.

**Disponibilidade por formato — medido no M3, e diferente do previsto.** Cortando um sinal de 44100 quadros em dois arquivos e somando as partes:

| Formato | Soma das partes | Diferença |
|---|---|---|
| WAV | 44100 | 0 |
| FLAC | 44100 | 0 |
| MP3 | 44100 | 0 |
| AAC/M4A | 44100 | 0 |
| Opus | 44101 | +1 quadro (+0,02 ms) |
| Ogg Vorbis | 43844 | **−256 quadros (−5,8 ms)** |

A previsão da versão 0.1 era o contrário: MP3 constava como caso condicional e Ogg Vorbis como caso garantido. Na prática o LAME grava a tag Xing/LAME por padrão e o libavcodec a usa, então MP3 fecha exato; já o corte de Ogg Vorbis em fronteira de página perde 256 quadros no material, e o que o arquivo não contém nenhum player recupera. É o mesmo desvio observado no M1, quando o tom de 0,5 s em Ogg reproduziu 0,497 s.

### Perda do dispositivo

miniaudio notifica troca ou remoção. O engine pausa, tenta reabrir o dispositivo padrão até 3 vezes com espera crescente e retoma na mesma posição. Se todas falharem, vai para `Error`. O thread de decodificação não é destruído e a posição é preservada.

---

## 6. Espectro e osciloscópio

| Parâmetro | Valor |
|---|---|
| Tamanho da FFT | 1024 amostras |
| Janela | Hann |
| Salto entre quadros | ~1/60 s |
| Normalização | `2/(N · ganho_coerente)` — senoide de amplitude 1.0 dá 0 dBFS no bin |
| Faixa dinâmica | -70 dBFS a 0 dBFS |
| Barras | 19, distribuição logarítmica de ~60 Hz a Nyquist |
| Acima de Nyquist | Barras cujo limite inferior ultrapassa `sr/2` ficam vazias |
| Suavização | Subida α 0.6, queda α 0.15 |
| Picos | Retenção 500 ms, depois queda acelerada |
| Silêncio | Magnitude presa em `1e-10` antes do log |

### Combinação dos canais estéreo

Somar L+R antes da FFT faz o espectro desaparecer em material com fases opostas. A combinação acontece **depois** da transformada:

```
mag[k] = sqrt( (|L[k]|² + |R[k]|²) / 2 )
```

Os dois espectros saem de uma única FFT complexa empacotando `z[n] = L[n] + j·R[n]` e separando as partes par e ímpar do resultado — o truque padrão de duas FFTs reais pelo preço de uma.

O osciloscópio usa a mistura mono `(L+R)/2`, onde anti-fase corretamente aparece como linha reta.

`Playing` desenha o sinal, `Paused` congela o último quadro, `Stopped` vai a zero. Desligar a visualização desliga a captura na origem (§3).

**Proibido e não utilizado**: número aleatório, animação pré-calculada, dado de demonstração.

---

## 7. Equalizador

Dez bandas, estado independente por canal, preamp. Faixa: **±12 dB** por banda e no preamp.

### Tipo de filtro e Q por banda

Bandas 1 e 10 são **shelving** (low-shelf e high-shelf); as bandas 2 a 9 são **peaking**, todas RBJ.

O Q de cada banda peaking vem da distância geométrica aos vizinhos. Para a banda *i* com vizinhas *f₍ᵢ₋₁₎* e *f₍ᵢ₊₁₎*:

```
f_inf = sqrt(f[i-1] · f[i])          f_sup = sqrt(f[i] · f[i+1])
BW    = log2(f_sup / f_inf)          (largura de banda em oitavas)
Q     = sqrt(2^BW) / (2^BW - 1)
Q     = clamp(Q, 0.7, 4.0)
```

Valores resultantes, que é o que vai no código:

| # | Freq. | Tipo | BW (oitavas) | Q |
|---|---|---|---|---|
| 1 | 60 Hz | low-shelf | — | S = 0.7 |
| 2 | 170 Hz | peaking | 1.185 | 1.18 |
| 3 | 310 Hz | peaking | 0.910 | 1.56 |
| 4 | 600 Hz | peaking | 0.845 | 1.68 |
| 5 | 1 kHz | peaking | 1.161 | 1.21 |
| 6 | 3 kHz | peaking | 1.293 | 1.08 |
| 7 | 6 kHz | peaking | 1.000 | 1.41 |
| 8 | 12 kHz | peaking | 0.611 | 2.34 |
| 9 | 14 kHz | peaking | 0.208 | **4.00** (limitado; calculado 6.94) |
| 10 | 16 kHz | high-shelf | — | S = 0.7 |

O limite superior de 4.0 existe porque 12, 14 e 16 kHz estão a menos de meia oitava umas das outras: o Q calculado para 14 kHz seria 6.94, audivelmente ressonante. O preço do limite é sobreposição entre as bandas agudas — dois sliders adjacentes em +12 dB somam mais de +12 dB na região comum. Isso é comportamento normal de equalizador gráfico e não conflita com EQ-03, que mede **uma banda por vez**.

A curva exata do Winamp original nunca foi publicada. Esta é documentada e medível, que é o que a especificação pede quando não há referência.

### Nyquist

Banda com frequência central ≥ `0.45 · sr` é substituída por identidade, não por filtro instável, e o slider correspondente aparece desabilitado. A 44,1 e 48 kHz todas operam; a 22,05 kHz as três superiores ficam inativas.

### Mudança suave

O ganho alvo de cada banda é interpolado ao longo de ~30 ms, com recálculo de coeficientes a cada sub-bloco de 64 amostras.

### Bypass

O bypass retira **todo o estágio do equalizador, preamp incluído**. Preamp é parte do equalizador, não um ganho independente; deixá-lo ativo em bypass tornaria o bypass uma meia-medida.

Há crossfade de 10 ms entre seco e processado para não estalar. A garantia de identidade é: **concluído o crossfade**, a saída do estágio do equalizador é bit a bit igual à entrada. Durante os 10 ms de transição não há identidade, por definição — o teste EQ-04 mede depois do crossfade e verifica separadamente que a transição não tem descontinuidade.

### Presets

Clássicos embutidos mais criação, edição, salvamento e exclusão de presets do usuário, em `eq_presets.json`.

---

## 8. Aparência e janelas

### Referência visual

Winamp 2.x / 5.x Classic, skin base, 275×116 px para o painel principal e para o equalizador. Playlist redimensionável a partir de 275×116.

### Origem dos assets

Os bitmaps do skin base são material protegido e não são redistribuídos. O atlas é **redesenhado do zero** seguindo a linguagem visual da especificação. Divergências entram em `docs/APROXIMACOES.md`.

### Escala

Apenas **escala inteira** (1×, 2×, 3×), vizinho mais próximo. Escala fracionária borraria pixel art e desalinharia os mostradores. As áreas clicáveis escalam junto.

### Modo integrado é o modo garantido

A versão 0.1 prometia encaixe magnético entre janelas de topo separadas. Isso depende de o cliente poder ler e definir a posição global da própria janela — coisa que o **Wayland não permite** (`xdg-shell` não expõe posição absoluta nem aceita posicionamento pelo cliente), e o Qt não contorna restrição de compositor. Como o ambiente principal de desenvolvimento é Wayland, a promessa estava quebrada na própria máquina de origem.

O desenho corrigido tem dois modos:

**Modo integrado (padrão em todo lugar, garantido em todo lugar).** Os três painéis vivem em **uma única janela**, empilhados na ordem clássica. Exibir e ocultar cada painel redimensiona a janela. Modo compacto reduz ao painel principal em forma de barra. Toda a persistência de layout se aplica. Não depende de nenhuma capacidade do compositor.

**Modo destacado (melhor esforço, por plataforma).** Painéis como janelas de topo independentes, com encaixe magnético de 10 px e movimento em grupo.

O movimento em grupo é **assimétrico**, como no Winamp: arrastar o painel principal leva junto tudo o que estiver encostado nele, direta ou indiretamente; arrastar uma janela secundária move só ela, e é assim que se desprende uma do conjunto. A primeira implementação formava o grupo a partir de qualquer janela arrastada, e o resultado, relatado em uso, foi que nada nunca se separava — puxar a playlist trazia o player inteiro atrás. Magnético em toda direção não é magnetismo, é cola.

| Plataforma | Destacado | Encaixe / movimento em grupo |
|---|---|---|
| Windows | sim | sim |
| macOS | sim | sim |
| Linux X11 | sim | sim |
| Linux Wayland | **não** | **não** — opção desabilitada com explicação na interface |

A interface não oferece um controle que não funciona: no Wayland a opção de destacar aparece **desabilitada**, com a razão escrita ao lado, em vez de aceitar o clique e não fazer nada.

Uma versão anterior desta tabela dizia que o modo destacado funcionava no Wayland, "posicionado pelo compositor". Não funcionava: sem posicionar, destacar produz três janelas espalhadas onde o compositor quiser, que não encaixam e não se movem juntas — o clique sem efeito que AP-18 proíbe. A recusa vive em `set_detached()`, e não só no menu, para que o atalho `Ctrl+D` concorde com o que a interface mostra.

### Recuperação de janela fora da área visível

A regra de "interseção mínima de 64×64 px" da versão 0.1 estava errada: classificaria como fora da tela uma barra compacta de 275×14 px perfeitamente visível.

A regra correta olha a **área de arraste**, que é o que o usuário precisa alcançar para resgatar a janela:

> Considere a faixa superior da janela, de altura `min(16, altura_da_janela)`. A janela é considerada visível se a interseção dessa faixa com a geometria disponível de alguma tela tiver pelo menos **64 px de largura**. Caso contrário, é reposicionada na tela primária.

Funciona para a janela cheia, para a barra compacta e para painéis destacados.

---

## 9. Persistência

Em `QStandardPaths::AppConfigLocation`:

| Arquivo | Conteúdo |
|---|---|
| `config.json` | Volume, balanço, shuffle, repeat, dispositivo, modo ReplayGain, escala, preferências |
| `layout.json` | Modo (integrado/destacado), posição, tamanho, visibilidade e agrupamento dos painéis |
| `eq_presets.json` | Presets do usuário + estado atual das bandas e do preamp |
| `session.m3u8` | Playlist da sessão, faixa atual e posição |

Gravação atômica via `QSaveFile`. Arquivo inválido é renomeado para `*.bad`, os padrões são carregados e o fato vai para o log — o aplicativo nunca se recusa a abrir por configuração corrompida.

---

## 10. Log

Log assíncrono: o thread de áudio só enfileira códigos e valores numéricos pré-alocados; a formatação acontece fora. Nenhuma escrita de arquivo, nenhuma alocação e nenhum `printf` dentro do callback.

URLs passam por redação antes de qualquer registro: `user:senha@host` e parâmetros de consulta conhecidos por carregar token são substituídos. Vale também para mensagens de erro exibidas na interface.

---

## 11. Estratégia de testes

`core/` roda sem Qt e sem placa de som, nos três sistemas, em CI.

### Verificações permanecem ativas em release

`assert()` desaparece com `NDEBUG`, e `RelWithDebInfo` define `NDEBUG`. Um teste cujas verificações somem na configuração em que o produto é construído não é teste. Toda verificação usa:

```cpp
PANG_CHECK(cond, msg)   // sempre avaliada, independente de NDEBUG;
                        // registra falha, continua, e o processo sai != 0
```

O binário de teste conta falhas e devolve código de saída diferente de zero. Sem framework, sem fixture.

### Matriz

| Área | Teste | Critério |
|---|---|---|
| FFT | Senoide 1 kHz @ 44.1 kHz | Pico no bin 23 ±1, amplitude 0 dBFS ±0.5 dB |
| FFT | Silêncio absoluto | Nenhum `NaN`/`inf`, tudo no piso |
| FFT | L e R em oposição de fase | Energia dentro de 3 dB da do canal isolado |
| EQ | Sweep por banda, uma por vez | Resposta dentro de ±1 dB do ganho pedido |
| EQ | Bypass, medido **após** o crossfade | Saída do estágio bit a bit igual à entrada |
| EQ | Transição de bypass | Sem descontinuidade de primeira derivada |
| EQ | Preamp em bypass | Também neutralizado |
| EQ | Banda acima de Nyquist | Identidade, saída estável |
| Limitador | Sinal a 0 dBFS com preamp +12 dB | **Nenhuma amostra** acima de -1.0 dBFS |
| Limitador | Transiente de 1 amostra a 0 dBFS | Contido pelo lookahead; contador do clamp = 0 |
| Limitador | Latência reportada | Igual ao lookahead + buffer do dispositivo |
| Ganho | Volume e balanço | Relação entre canais dentro de ±0.1 dB |
| Ganho | Mudança abrupta de volume | Derivada limitada pela rampa |
| Gapless | Sinal de rampa contínua cortado em dois arquivos | Saída contém a sequência original **sem amostra a mais nem a menos** na junção |
| Gapless | Faixas com taxas diferentes em sequência | Mesma contagem exata após conversão para a taxa do dispositivo |
| Gapless | Playlist alterada durante a pré-carga | Pré-carga descartada, faixa correta tocada |
| Vis | Ring cheio | Bloco descartado, contador incrementado, dado nunca rasgado |
| Vis | Captura desligada | Nenhuma cópia no callback |
| Estado | Seqlock sob escrita concorrente | Leitor nunca observa campo de gerações diferentes |
| Estado | Resposta assíncrona de geração antiga | Descartada, estado atual intacto |
| Rede | Cancelamento durante abertura bloqueante | `AVIOInterruptCB` retorna em < 100 ms |
| Playlist | Round-trip M3U/M3U8/PLS | Ordem, caminhos relativos e títulos preservados |
| Playlist | Shuffle de N itens | Ciclo completo sem repetição, histórico retrocede |
| Playlist | 10 000 itens | UI nunca bloqueada por mais de 50 ms |
| Decodificação | Cada formato declarado | Decodifica, informa taxa/canais, faz seek |
| Robustez | Ausente, truncado, sem permissão, tag inválida | `Error`, sem crash |

### Teste de gapless, em detalhe

O critério anterior — "descontinuidade abaixo de -60 dBFS" — não servia: duas músicas arbitrárias podem ter descontinuidade natural na junção, e um teste que passa por acidente não é teste.

O procedimento correto usa um sinal em que **cada amostra é identificável**: uma rampa de contador, cortada em dois arquivos em um ponto exato. Tocados em sequência com gapless, a saída capturada deve conter a sequência original inteira, **sem inserção nem remoção de amostras** na junção — o que mede diretamente os dois defeitos possíveis.

WAV primeiro, para testar o mecanismo de emenda isoladamente (sem delay de encoder). Depois MP3, Opus, Vorbis e AAC, para testar o tratamento de delay e padding. Em formatos com perda a comparação de amplitude tem tolerância, mas a **contagem de amostras continua exata**.

### Verificação manual

Registrada como manual, nunca como automatizada: fidelidade visual, encaixe de janelas por plataforma, teclas de mídia, MPRIS/SMTC/MPNowPlayingInfoCenter, perda física de dispositivo de áudio, corte de rede durante stream.

---

## Mudanças da versão 0.1 para a 0.2

Todas motivadas por revisão técnica, e todas corrigem afirmações que estavam erradas ou incompletas:

1. **Limitador** (§5) — sem lookahead não havia como garantir o teto prometido. Agora: lookahead de 1,5 ms com latência documentada, clamp rígido como garantia dura, e a distinção explícita entre pico de amostra (garantido, -1.0 dBFS) e true peak (não prometido).
2. **Ring de visualização** (§3) — sobrescrever dado não consumido era corrida. Agora: descarte com contador, captura desligável na origem, índice de leitura avançado só após a cópia.
3. **Bloco de estado** (§3) — "bloco atômico" presumia lock-free onde não há. Agora: seqlock, com `static_assert` sobre o único tipo assumido.
4. **Gapless** (§5, §11) — estava definido por impressão auditiva. Agora: quem apara, em que taxa, drenagem do `swr`, mudança de taxa entre faixas, invalidação da pré-carga; e teste por contagem exata de amostras com sinal de rampa.
5. **macOS** (§1, §2) — acrescentado como alvo. Três sistemas construídos e testados desde o M0.
6. **Critério de portabilidade** (§2) — "zero linhas em `core/`" substituído por núcleo único, sem API de sistema, aprovado nos testes dos três sistemas.
7. **Wayland** (§8) — encaixe entre janelas de topo é impossível no Wayland. Modo integrado em janela única passa a ser o modo padrão e garantido; destacado vira melhor esforço com matriz por plataforma.
8. **I/O cancelável** (§3) — `AVIOInterruptCB` e timeouts, antecipados para o M1.
9. **Q do equalizador** (§7) — fórmula e tabela de valores por banda, em vez de "estreitado proporcionalmente"; shelving nas extremidades; limite de 4.0 com o preço declarado.
10. **Bypass** (§7) — esclarecido que inclui o preamp e que a identidade vale após o crossfade.
11. **Testes** (§11) — `PANG_CHECK` em lugar de `assert()`, que some com `NDEBUG`.
12. **Recuperação de janela** (§8) — regra baseada na faixa de arraste, em vez de 64×64 px, que reprovaria a barra compacta.
