# PlayAmpNG — Matriz de Requisitos

Versão 0.2 — revisão após crítica técnica do M0. Alvos: Linux, Windows e macOS.

Atualizado ao fim do M0. Evidências em `TEST_REPORT.md`. Nenhum requisito pode ser marcado como `OK` sem evidência registrada em `docs/TEST_REPORT.md`.

Legenda de verificação:
- **AUTO** — teste automatizado em `tests/`, executado por CTest.
- **MANUAL** — inspeção ou procedimento manual, com resultado registrado.
- **MEDIDO** — medição numérica com limite definido antes da execução.

Legenda de estado: `PENDENTE` · `OK` · `PARCIAL` · `NÃO VERIFICADO` · `FORA DE ESCOPO`

---

## AP — Aparência (spec §2)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| AP-01 | Janela principal 275×116 px, retangular e de alta densidade | MANUAL | M5 | OK (M5) |
| AP-02 | Paleta em tons escuros de cinza, mostradores e texto em verde | MANUAL | M5 | OK (M5) |
| AP-03 | Bordas chanfradas com luz no topo-esquerda | MANUAL | M5 | OK (M5) |
| AP-04 | Tipografia bitmap compacta e legível em 1× | MANUAL | M5 | OK (M5) |
| AP-05 | Botões com estados normal, pressionado, ativo, desabilitado e foco | MANUAL | M5 | OK (M5) |
| AP-06 | Três painéis: principal, equalizador, playlist | MANUAL | M5 | OK (M5) |
| AP-07 | Exibição e ocultação independentes de cada painel | AUTO + MANUAL | M5 | OK (M5) |
| AP-08 | Encaixe magnético entre janelas destacadas (limiar 10 px), onde a plataforma permite | MANUAL | M5 | PENDENTE |
| AP-09 | Janelas destacadas encostadas movem-se como grupo, onde a plataforma permite | MANUAL | M5 | PENDENTE |
| AP-10 | Persistência de posição, tamanho e visibilidade | AUTO | M5 | OK (M5) |
| AP-11 | Modo compacto (barra) | MANUAL | M5 | OK (M5) |
| AP-12 | Escala inteira 1×/2×/3× sem recorte de texto | MANUAL | M5 | OK (M5) |
| AP-13 | Áreas clicáveis escalam junto com a interface | MANUAL | M5 | OK (M5) |
| AP-14 | Recuperação de janela fora da área visível: faixa de arraste (altura min(16,h)) com < 64 px visíveis → realoca | AUTO | M5 | OK (M5) |
| AP-15 | Ausência de seleção de skins, importação de temas ou personalização estrutural | MANUAL | M5 | OK (M5) |
| AP-16 | Atlas de sprites redesenhado, sem redistribuir assets de terceiros | MANUAL | M5 | OK (M5) |
| AP-17 | Modo integrado: três painéis em uma janela, funcional em todas as plataformas, inclusive Wayland | MANUAL | M5 | OK (M5) |
| AP-18 | Modo destacado: onde o encaixe é indisponível, a opção aparece desabilitada com explicação, nunca aceita o clique sem efeito | MANUAL | M5 | PENDENTE |

## PL — Player principal (spec §3)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| PL-01 | Reproduzir | AUTO | M1 | OK (M1) |
| PL-02 | Pausar e continuar na mesma posição | AUTO | M1 | OK (M1) |
| PL-03 | Parar: estado `Stopped`, posição 0, faixa mantida | AUTO | M1 | OK (M1) |
| PL-04 | Faixa anterior sempre vai à anterior, nunca reinicia a atual | AUTO | M2 | OK (M2) |
| PL-05 | Próxima faixa | AUTO | M2 | OK (M2) |
| PL-06 | Abrir arquivos por diálogo | MANUAL | M2 | OK (M2) |
| PL-07 | Abrir diretório, com busca recursiva opcional | AUTO + MANUAL | M2 | OK (M2) |
| PL-08 | Abrir URL de áudio | AUTO | M6 | OK (M6) |
| PL-09 | Volume com efeito real e rampa suave | MEDIDO | M3 | OK (M3) |
| PL-10 | Balanço estéreo com efeito real | MEDIDO | M3 | OK (M3) |
| PL-11 | Shuffle ligado/desligado | AUTO | M2 | OK (M2) |
| PL-12 | Repetição: desligada, da faixa, da playlist | AUTO | M2 | OK (M2) |
| PL-13 | Exibir/ocultar equalizador e playlist pelo player | MANUAL | M5 | OK (M5) |
| PL-14 | Display: nome da faixa com rolagem para títulos longos | MANUAL | M5 | OK (M5) |
| PL-15 | Display: tempo decorrido, alternável para tempo restante | AUTO | M5 | OK (M5) |
| PL-16 | Display: estado da reprodução | MANUAL | M5 | OK (M5) |
| PL-17 | Display: bitrate quando disponível, vazio quando não | AUTO | M5 | OK (M5) |
| PL-18 | Display: frequência de amostragem | AUTO | M5 | OK (M5) |
| PL-19 | Display: indicação mono/estéreo | AUTO | M5 | OK (M5) |
| PL-20 | Display: posição da faixa na playlist | AUTO | M5 | OK (M5) |
| PL-21 | Barra de progresso acompanha a posição real do backend | MEDIDO | M1 | OK (M1) |
| PL-22 | Busca temporal quando a fonte suporta | AUTO | M1 | OK (M1) |
| PL-23 | Busca desabilitada quando a fonte não suporta | AUTO | M6 | PENDENTE |
| PL-24 | Fim da playlist: repeat=off para; repeat=all reinicia; repeat=track repete | AUTO | M2 | OK (M2) |
| PL-25 | Troca de faixa durante pausa mantém o estado pausado, posição 0 | AUTO | M2 | OK (M2) |
| PL-26 | Duração, bitrate e progresso nunca são inventados quando ausentes | AUTO | M2 | OK (M2) |

## AU — Engine de áudio (spec §4)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| AU-01 | Decodificação por biblioteca consolidada, sem codec próprio | MANUAL | M1 | OK (M1) |
| AU-02 | Reprodução real de MP3 | AUTO | M1 | OK (M1) |
| AU-03 | Reprodução real de WAV | AUTO | M1 | OK (M1) |
| AU-04 | Reprodução real de FLAC | AUTO | M1 | OK (M1) |
| AU-05 | Reprodução real de Ogg Vorbis | AUTO | M1 | OK (M1) |
| AU-06 | Reprodução real de Opus | AUTO | M1 | OK (M1) |
| AU-07 | Reprodução real de AAC/M4A | AUTO | M1 | OK (M1) |
| AU-08 | Disponibilidade efetiva de cada formato documentada por pacote distribuído | MANUAL | M7 | PENDENTE |
| AU-09 | Processamento interno em ponto flutuante (f32) | MANUAL | M1 | OK (M1) |
| AU-10 | Mudanças de ganho com rampa, sem estalo | MEDIDO | M3 | OK (M3) |
| AU-11 | Seleção de dispositivo de saída | AUTO | M6 | PARCIAL (M6) |
| AU-12 | Tratamento de desconexão do dispositivo: reabrir até 3×, preservar posição | AUTO | M6 | PARCIAL (M6) |
| AU-13 | Gapless: sinal de rampa cortado em dois arquivos reproduz sem amostra a mais nem a menos na junção | MEDIDO | M3 | OK (M3) |
| AU-14 | Matriz de disponibilidade de gapless por formato, medida e não presumida | MEDIDO | M3 | OK (M3) |
| AU-15 | Leitura de ReplayGain, modos por faixa e por álbum | AUTO | M3 | OK (M3) |
| AU-16 | Anti-clipping: nenhuma amostra da saída acima de -1.0 dBFS (pico de amostra; true peak não é prometido) | MEDIDO | M3 | OK (M3) |
| AU-17 | Indicador de clipping na interface quando o limitador atua | MANUAL | M5 | OK (M3) |
| AU-18 | Ordem dos estágios de processamento documentada | MANUAL | M0 | OK (M0) |
| AU-19 | Ponto de captura da visualização documentado | MANUAL | M0 | OK (M0) |
| AU-20 | Disco, rede, metadados e interface fora do processamento crítico | MANUAL | M1 | OK (M1) |
| AU-21 | Nenhuma operação bloqueante, log síncrono ou alocação no callback de áudio | MANUAL + MEDIDO | M3 | OK (M3) |
| AU-22 | Limitador com lookahead contém transiente de 1 amostra a 0 dBFS; contador do clamp rígido permanece zero | MEDIDO | M3 | OK (M3) |
| AU-23 | Latência total reportada = lookahead do limitador + buffer do dispositivo | MEDIDO | M3 | OK (M3) |

## VI — Visualização (spec §5)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| VI-01 | Espectro alimentado por amostras reais do áudio em reprodução | AUTO | M4 | OK (M4) |
| VI-02 | FFT com janela Hann | AUTO | M4 | OK (M4) |
| VI-03 | Tamanho da FFT, normalização, faixa dinâmica e taxa de atualização documentados | MANUAL | M0 | OK (M0) |
| VI-04 | Senoide de 1 kHz @ 44.1 kHz → pico no bin correto (±1) | AUTO | M4 | OK (M4) |
| VI-05 | Senoide de amplitude 1.0 → 0 dBFS ±0.5 dB no bin | AUTO | M4 | OK (M4) |
| VI-06 | Agrupamento em 19 barras com distribuição logarítmica | MANUAL | M4 | OK (M4) |
| VI-07 | Magnitudes em escala logarítmica de amplitude | AUTO | M4 | OK (M4) |
| VI-08 | Suavização temporal com subida e queda distintas | MANUAL | M4 | OK (M4) |
| VI-09 | Retenção e queda de picos | MANUAL | M4 | OK (M4) |
| VI-10 | Cores coerentes com a aparência clássica | MANUAL | M5 | OK (M5) |
| VI-11 | Barras acima de Nyquist da fonte permanecem vazias | AUTO | M4 | OK (M4) |
| VI-12 | Silêncio não gera `NaN` nem `inf` | AUTO | M4 | OK (M4) |
| VI-13 | Canais em oposição de fase não fazem o espectro desaparecer | AUTO | M4 | OK (M4) |
| VI-14 | Combinação estéreo documentada (magnitudes pós-FFT) | MANUAL | M0 | OK (M0) |
| VI-15 | Silêncio, pausa e parada representados corretamente e de forma distinta | MANUAL | M4 | OK (M4) |
| VI-16 | Modo osciloscópio com representação temporal real | MANUAL | M4 | OK (M4) |
| VI-17 | Desligar a visualização desliga a captura na origem: nenhuma cópia executada no callback | MEDIDO | M4 | OK (M4) |
| VI-18 | Atualização independente da interface geral | MANUAL | M4 | OK (M4) |
| VI-19 | Transferência de amostras não bloqueia o thread de áudio | MANUAL | M4 | OK (M4) |
| VI-20 | Ausência de número aleatório, animação pré-calculada ou dado de demonstração | MANUAL | M4 | OK (M4) |
| VI-21 | Ring de captura descarta bloco quando cheio, nunca sobrescreve região sob leitura, e conta os descartes | AUTO | M4 | OK (M4) |

## EQ — Equalizador (spec §6)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| EQ-01 | Dez bandas estéreo nas frequências de referência + preamp | AUTO | M3 | OK (M3) |
| EQ-02 | Ajuste de ganho em dB, faixa ±12 dB documentada | AUTO | M3 | OK (M3) |
| EQ-03 | Resposta medida por banda dentro de ±1 dB do ganho pedido | MEDIDO | M3 | OK (M3) |
| EQ-04 | Bypass retira a equalização: após o crossfade, saída do estágio bit a bit igual à entrada | AUTO | M3 | OK (M3) |
| EQ-05 | Reset para resposta plana | AUTO | M3 | OK (M3) |
| EQ-06 | Presets integrados | MANUAL | M3 | OK (M3) |
| EQ-07 | Criar, editar, salvar e excluir presets do usuário | MANUAL | M5 | PENDENTE |
| EQ-08 | Mudanças aplicadas suavemente durante reprodução, sem estalo | MEDIDO | M3 | OK (M3) |
| EQ-09 | Persistência dos ajustes entre sessões | AUTO | M3 | OK (M3) |
| EQ-10 | Tipo de filtro, largura de banda e Q documentados | MANUAL | M0 | OK (M0) |
| EQ-11 | Banda com centro ≥ 0.45·sr vira identidade, sem instabilidade | AUTO | M3 | OK (M3) |
| EQ-12 | Sliders de bandas inativas aparecem desabilitados | MANUAL | M5 | OK (M3) |
| EQ-13 | Tipo de filtro, fórmula de Q e tabela de valores por banda documentados e implementáveis | MANUAL | M0 | OK (M0) |
| EQ-14 | Bypass neutraliza também o preamp | AUTO | M3 | OK (M3) |
| EQ-15 | Transição de bypass sem descontinuidade de primeira derivada | MEDIDO | M3 | OK (M3) |

## LI — Playlist (spec §7)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| LI-01 | Inclusão de arquivos e pastas por diálogo | MANUAL | M2 | OK (M2) |
| LI-02 | Inclusão por drag-and-drop | MANUAL | M2 | OK (M2) |
| LI-03 | Busca recursiva opcional em diretórios | AUTO | M2 | OK (M2) |
| LI-04 | Reordenação por arrastar | MANUAL | M5 | PENDENTE — painel em sprites ainda sem arraste |
| LI-05 | Seleção múltipla | MANUAL | M5 | OK (M5) |
| LI-06 | Remoção de itens sem excluir os arquivos originais | AUTO | M2 | OK (M2) |
| LI-07 | Limpeza da lista | AUTO | M2 | OK (M2) |
| LI-08 | Ordenação por título, artista, álbum, duração e caminho | AUTO | M2 | OK (M2) |
| LI-09 | Busca textual | AUTO | M2 | OK (M2) |
| LI-10 | Destaque da faixa em reprodução, distinta da selecionada | MANUAL | M5 | OK (M2) |
| LI-11 | Duração individual e total conhecida (sem somar durações desconhecidas) | AUTO | M2 | OK (M2) |
| LI-12 | Importação e exportação de M3U, M3U8 e PLS (round-trip preserva ordem) | AUTO | M2 | OK (M2) |
| LI-13 | Resolução correta de caminhos relativos em playlists | AUTO | M2 | OK (M2) |
| LI-14 | Shuffle mantém histórico de navegação | AUTO | M2 | OK (M2) |
| LI-15 | Shuffle não repete até fechar o ciclo, salvo repeat=track ativo | AUTO | M2 | OK (M2) |
| LI-16 | Leitura de metadados assíncrona | MANUAL | M2 | OK (M2) |
| LI-17 | Lista virtualizada: 10 000 itens sem travamento | MEDIDO | M2 | OK (M2) |

## MD — Metadados e streaming (spec §8)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| MD-01 | Ler título, artista, álbum, nº da faixa, gênero, ano e duração quando disponíveis | AUTO | M2 | OK (M2) |
| MD-02 | Sem tags, usar o nome do arquivo | AUTO | M2 | OK (M2) |
| MD-03 | Janela de propriedades técnicas e localização do arquivo | MANUAL | M6 | PENDENTE |
| MD-04 | Estados de conexão, buffering, reprodução e erro em stream | MANUAL | M6 | PARCIAL (M6) |
| MD-05 | Leitura de metadados ICY quando fornecidos | AUTO | M6 | PARCIAL (M6) |
| MD-06 | Tratamento de redirecionamentos HTTP | AUTO | M6 | OK (M6) |
| MD-07 | Reconexão com número e intervalo de tentativas limitados | AUTO | M6 | OK (M6) |
| MD-08 | Busca temporal desabilitada em fonte não pesquisável | AUTO | M6 | OK (M6) |
| MD-09 | Stream ao vivo nunca exibido como arquivo de duração finita | AUTO | M6 | OK (M6) |

## IN — Integração e persistência (spec §9)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| IN-01 | Atalhos de teclado documentados | MANUAL | M6 | PENDENTE |
| IN-02 | Todos os controles operáveis por teclado | MANUAL | M6 | OK (M5) |
| IN-03 | Nomes acessíveis e tooltips | MANUAL | M6 | PENDENTE |
| IN-04 | Integração com teclas multimídia | MANUAL | M6 | PENDENTE |
| IN-05 | Integração MPRIS no Linux | MANUAL | M6 | PENDENTE |
| IN-06 | Opção de manter a janela acima das demais | MANUAL | M6 | PENDENTE |
| IN-07 | Abertura de arquivos por argumentos de linha de comando | AUTO | M1 | OK (M1) |
| IN-08 | Arquivo passado a uma instância já aberta é enfileirado nela | MANUAL | M6 | PENDENTE |
| IN-09 | Gravação atômica de configuração, playlist, presets e layout | AUTO | M5 | OK (M5) |
| IN-10 | Configuração inválida → renomeia para `.bad`, carrega padrões, registra log | AUTO | M5 | OK (M5) |
| IN-11 | Restaurar sessão não inicia áudio, salvo preferência explícita | AUTO | M5 | PENDENTE |

## AR — Arquitetura e qualidade (spec §10)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| AR-01 | Responsabilidades separadas conforme §10 | MANUAL | M0 | OK (M0) |
| AR-02 | Estados explícitos: Stopped, Loading, Playing, Paused, Buffering, Error | AUTO | M1 | OK (M1) |
| AR-03 | Resposta assíncrona de geração antiga é descartada sem efeito | AUTO | M2 | OK (M2) |
| AR-04 | Interface reflete o estado confirmado pelo backend, inclusive em falha | AUTO | M1 | OK (M1) |
| AR-05 | Logs úteis para diagnóstico | MANUAL | M1 | PENDENTE |
| AR-06 | Credenciais em URL nunca registradas em log nem exibidas | AUTO | M6 | PARCIAL (M1) — funcao de redacao verificada; integracao com streaming em M6 |
| AR-07 | `core/` não inclui Qt nem API de sistema operacional | AUTO (verificação de build) | M0 | OK (M0) |
| AR-08 | `core/` permanece único e compartilhado, sem API de sistema, e passa na íntegra a suíte nos três sistemas | AUTO | M8/M9 | PENDENTE |
| AR-09 | I/O de rede e disco cancelável (`AVIOInterruptCB` + timeout): cancelamento retorna em < 100 ms | AUTO | M1 | PARCIAL (M1) — mecanismo ativo e parada pronta; cancelamento de abertura de rede bloqueada so em M6 |
| AR-10 | Verificações de teste permanecem ativas em build de release (`PANG_CHECK`, não `assert`) | AUTO | M0 | OK (M0) |
| AR-11 | Estado publicado por seqlock; nenhum tipo presumido lock-free sem `static_assert` | AUTO | M1 | OK (M1) |

## RB — Robustez (spec §11)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| RB-01 | Arquivo ausente → `Error`, sem crash, playlist intacta | AUTO | M2 | OK (M1) |
| RB-02 | Arquivo corrompido ou truncado → `Error`, sem crash | AUTO | M2 | OK (M2) |
| RB-03 | Arquivo sem permissão de leitura → `Error`, sem crash | AUTO | M2 | OK (M2) |
| RB-04 | Interrupção de stream → reconexão ou `Error` explícito | AUTO | M6 | OK (M6) |
| RB-05 | Perda do dispositivo de áudio → recuperação ou `Error` explícito | AUTO | M6 | PARCIAL (M6) |
| RB-06 | Carga de playlist extensa sem travar a interface | MEDIDO | M2 | OK (M2) |
| RB-07 | Execução prolongada: CPU, memória e interrupções de áudio registradas | MEDIDO | M7 | PENDENTE |

## EN — Entregáveis (spec §12)

| ID | Requisito | Verificação | Etapa | Estado |
|---|---|---|---|---|
| EN-01 | Código-fonte organizado conforme a árvore de `ARCHITECTURE.md` | MANUAL | M7 | PENDENTE |
| EN-02 | Instruções reproduzíveis de instalação, execução e build | MANUAL | M7 | PENDENTE |
| EN-03 | Dependências com versões e licenças identificadas | MANUAL | M7 | PENDENTE |
| EN-04 | Pacote executável para Linux (AppImage e/ou Flatpak) | MANUAL | M7 | PENDENTE |
| EN-05 | Documento de arquitetura | MANUAL | M0 | OK (M0) |
| EN-06 | Matriz de requisitos com estado e evidências | MANUAL | M7 | PENDENTE |
| EN-07 | Testes e relatório de execução | AUTO | M7 | PENDENTE |
| EN-08 | Limitações conhecidas documentadas | MANUAL | M7 | PENDENTE |
| EN-09 | Pacote executável para Windows | MANUAL | M8 | PENDENTE |
| EN-10 | Pacote executável para macOS (assinatura e notarização são decisão à parte) | MANUAL | M9 | PENDENTE |

## FE — Fora de escopo (spec §12, tratados como extensões)

| ID | Item | Estado |
|---|---|---|
| FE-01 | Biblioteca de mídia indexada | FORA DE ESCOPO |
| FE-02 | Vídeo | FORA DE ESCOPO |
| FE-03 | Gravação de CDs | FORA DE ESCOPO |
| FE-04 | Plugins externos | FORA DE ESCOPO |
| FE-05 | Visualizações tipo MilkDrop | FORA DE ESCOPO |
| FE-06 | Cálculo próprio de ReplayGain (scanner) | FORA DE ESCOPO |
| FE-07 | Suporte a skins de terceiros | FORA DE ESCOPO (proibido pela §2) |
