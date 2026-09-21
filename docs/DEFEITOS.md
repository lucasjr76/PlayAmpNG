# PlayAmpNG — Defeitos e observações de teste manual

Lista viva. Cada item diz quem observou, o que foi medido e onde foi corrigido. Itens abertos ficam no topo.

---

## Abertos

Nenhum. Os dois que estavam aqui foram resolvidos no M8: o encaixe entre painéis destacados (A-11) existe onde a plataforma permite posicionar janelas, e no Wayland a opção aparece desabilitada com a razão (AP-08, AP-09, AP-18); arrastar para reordenar a playlist (A-12) foi reimplementado no painel em sprites (LI-04).

---

## Corrigidos

### C-28 — Nenhum caminho na interface para abrir um endereço de rádio

**Encontrado ao preparar o teste manual de streaming.** Não havia diálogo de endereço, e um link arrastado do navegador para a playlist era ignorado em silêncio — só arquivo local entrava. O PL-08 estava OK porque passava pela linha de comando e pelos testes; um usuário não tinha como chegar lá.

**Correção:** Ctrl+L e item "Abrir endereço…" no menu, como no Winamp, entrando pela mesma rota de abrir arquivos e tocando na hora; e links http/https arrastados passam a entrar. Qualquer outro esquema (`ftp:`, `javascript:`) continua de fora.

### C-27 — O leitor de tags abria protocolo proibido e podia travar num servidor mudo

**Encontrado ao ligar o Ctrl+L, lendo o que acontece depois de incluir um endereço.** O leitor de tags abria a fonte **sem nenhuma** das opções de rede que o `probe` e o decodificador usam:

- sem a lista de protocolos permitidos: medido, `concat:` de dois arquivos locais **foi aberto** pelo leitor de tags, enquanto o `probe` o recusou. Uma playlist de terceiros podia fazer o player ler o que não devia pela porta das tags;
- sem prazo: num servidor que aceita a conexão e não responde, o thread de metadados ficaria preso indefinidamente, e as faixas seguintes da playlist sem duração.

Oitava dupla divergente deste projeto.

**Correção:** fonte remota não é aberta para ler tags — o que ela toca chega pelo ICY na reprodução; arquivo local abre com as mesmas opções do `probe`.

**Verificado por mutação:** sem a lista, o `concat:` volta a abrir; lendo fonte remota, a leitura fica presa 10 117 ms num servidor mudo. A primeira versão desse segundo teste usava uma porta fechada, onde a conexão é recusada na hora, e passava com ou sem a proteção — foi refeita contra um servidor mudo.

### C-26 — Rádio engasgada aparecia como "tocando", em silêncio

**Encontrado pelo CI:** o teste de buffering passou no Linux e reprovou no macOS.

**Causa:** a entrada em `Buffering` era decidida no laço do decodificador, logo depois de ler da rede. Com a rede parada, que é justamente o caso do buffering, o decodificador fica bloqueado na leitura e a verificação não roda; quando os dados voltam, ele escreve e só então olha o nível, que já subiu. O `render()` via o buffer secar e só contava uma interrupção de áudio. No Linux o teste passava por sorte de tempo.

**Correção:** cada lado decide o que observa. Quem consome declara o buffer secando, na hora; quem produz declara que encheu de novo. As duas transições usam troca condicional, para uma pausa do usuário não ser sobrescrita.

**O teste também estava errado.** O servidor mandava áudio devagar, e com dados chegando aos poucos a verificação antiga rodava de vez em quando e acertava por acaso — a implementação com defeito passava três de três no Linux. Agora o servidor manda 4 s e fica **mudo**: a implementação antiga reprova três de três, a nova passa.

### C-26 — Rádio engasgando aparecia como "tocando", em silêncio

**Encontrado pelo CI do macOS**, na verificação nova do MD-04 — que passava no Linux.

**Causa:** a entrada em `Buffering` era decidida no laço do decodificador, logo depois de ler da rede. Com a rede parada — exatamente o caso de buffering — o decodificador fica bloqueado na leitura e não roda a verificação; quando os dados voltam, ele escreve e só então olha o nível, que já subiu.

**Correção:** entrar em `Buffering` é decidido em `render()`, por quem esvazia o buffer e percebe a hora em que ele seca; sair, pelo decodificador, que sabe quando encheu. Troca condicional nas duas, para não sobrescrever uma pausa do usuário.

**O primeiro teste não separava o código certo do errado:** a implementação antiga passava três de três no Linux, porque o servidor de teste mandava dados devagar, e o decodificador rodava por acaso de vez em quando. O servidor passou a **parar** de mandar (`/engasga`); com a rede muda, a implementação antiga reprova três de três, e a nova passa.

### C-25 — Credencial de rádio exibida como título da faixa

**Encontrado ao fechar o AR-06, medindo em vez de supor.** Uma rádio sem tags tinha como título o "nome do arquivo" da URL:

```
http://usuario:SENHA@radio.example/        -> http://usuario:SENHA@radio.example/
.../stream?token=TOKEN                     -> stream?token=TOKEN
http://usuario:SENHA@radio.example:8000    -> usuario:SENHA@radio
```

Esse título ia para a playlist, a janela principal e o painel de mídia do sistema. A janela de propriedades mostrava o endereço cru, e o `xesam:url` o publicava cru no D-Bus.

**Correção:** título de fonte remota é o endereço redigido; propriedades e D-Bus também passam pela redação. Verificado lendo os metadados pelo D-Bus, de fora do player.

### C-24 — Modo compacto só com o tempo, sem dois-pontos e sem botões

**Observado em uso:** a barra compacta mostrava o logotipo, o tempo como `0010` e o texto fixo "PLAYAMP NG". Não havia botões — sair do compacto só com duplo-clique — nem controles de reprodução.

**Causas, medidas no renderizador sem tela:** o tempo reaproveitava a função do mostrador grande, que devolve quatro dígitos sem separador porque lá os dois-pontos vêm desenhados no fundo; e o compacto desenhava só a barra de título normal, sem nenhum controle.

**Correção:** disposição do *windowshade* do Winamp (`APROXIMACOES.md`), com título da faixa rolando, tempo `MM:SS`, mini-controles, barra de posição e os três botões. O título usa a mesma rota de desenho do modo normal, e a barra de posição a mesma conta para desenho e clique.

**Verificado:** `test_layout` confere a geometria do compacto e `test_compact` clica em cada controle. Seis mutações detectadas — duas áreas sobrepostas, título descentrado ou fora do poço, botão fora da barra, áreas de clique trocadas e o tempo sem dois-pontos. A primeira versão da verificação do tempo era vácua: com `0010`, a terceira célula recebe o dígito "1", que também tem tinta; agora confere também que existe uma quinta célula.

### C-23 — Segundo lançamento destruía o log da instância aberta

**Encontrado na análise do AR-05, lendo o código.** O arquivo de log era aberto antes da verificação de instância única. Abrir um arquivo pelo gerenciador com o player já aberto inicia um segundo processo, que só entrega o arquivo à janela existente e sai — mas antes renomeava o log da instância em uso para `.1` e truncava o arquivo. O log de diagnóstico se perdia justamente num uso comum.

**Correção:** o arquivo só é aberto depois de decidido que esta instância vai continuar rodando.

### C-22 — Dispositivo de áudio reaberto a cada troca de faixa

**Encontrado no primeiro log que registrou as transições da saída de áudio (AR-05).** Numa abertura normal, sem ninguém desconectar nada:

```
10:04:37.212 [info] audio: PulseAudio, "GB205 ...", 44100 Hz
10:04:37.310 [aviso] saida de audio perdida; reabrindo:
10:04:37.787 [info] saida de audio restabelecida
10:04:38.387 [aviso] saida de audio perdida; reabrindo:
```

**Causa, medida com instrumentação:** ao carregar uma faixa o controlador suspende o dispositivo; o miniaudio avisa `stopped`; e o tratador da notificação lia **todo** `stopped` como perda. O detector de parada já distinguia parada proposital de perda — a rota da notificação não. Duas rotas para a mesma decisão, divergindo: a sexta vez neste projeto.

```
DIAG suspend
DIAG notificacao tipo=1     <- "stopped", provocado pelo proprio suspend
DIAG resume
[aviso] saida de audio perdida; reabrindo
```

**Correção:** as duas rotas consultam a mesma intenção (`expected_running`). A linha de perda também passa a dizer qual rota disparou — antes saía com a razão em branco.

**Verificado:** a abertura não registra mais nenhuma perda; o teste com remoção real de dispositivo (`device_loss`, via `pactl`) continua detectando a perda verdadeira. **Por mutação:** a notificação volta a ignorar a intenção e `test_device` reprova três vezes, uma por troca de faixa simulada.

### C-21 — Sessão restaurada aparecia inteira sem duração

**Observado:** 225 faixas na playlist, todas com `--:--` e total `00:00+`.

**Causa — minha, introduzida no M7.** A restauração de sessão inseria as faixas **direto na playlist**, em vez de usar o mesmo caminho de abrir arquivos. Esse caminho faz três coisas, e a inserção direta fazia só a primeira:

```
add_paths()  ->  insere  ->  playlist_changed()  ->  refresh()  ->  scan_missing()
```

Sem `scan_missing()`, nenhuma faixa era varrida e todas ficavam com `duration_ms = -1`. O defeito só aparecia **depois de fechar e abrir** o player: abrindo com um diretório como argumento, o caminho correto era usado e as durações apareciam.

É a mesma classe de erro que já havia aparecido em `probe`/`decoder` no M6-1 e no desenho da barra de rolagem: **duas rotas para a mesma operação divergem**. A correção não foi acrescentar a chamada que faltava, e sim fazer a restauração passar por `add_paths`, que já existe.

**Verificado:** 225 arquivos MP3 varridos em menos de 20 s; a segunda execução mantém as durações. **Por mutação:** devolvida a inserção direta, o teste acusa `antes [60, 60], depois [-1, -1]`.

O scanner em si estava correto e não foi tocado.

### C-20 — Barra de rolagem flutuando e linha selecionada listrada de preto

Dois defeitos visuais encontrados em uso, na playlist.

| Observação | Causa | Correção |
|---|---|---|
| *"a barra de rolagem não fica na lateral direita, fica flutuando"* | Ela era posicionada com a margem da lista (14 px), como se fosse conteúdo. Barra de rolagem é **cromo de janela**, e cromo mora na borda — recuada, ela flutuava no meio do preto e parecia solta do que rola | Encostada na borda direita: 269..274 |
| *"música selecionada fica com selecionado AZUL e onde tem TEXTO com fundo preto, estranho ao olhar"* | O `text.bmp` do formato tem fundo preto. Desenhado por cima do azul da seleção, cada palavra levava junto uma caixa preta e a linha ficava **listrada** | Texto recortado (`draw_text` com cor), que já existia desde o M5 e não estava sendo usado nas linhas da lista |

A cor do texto também passou a distinguir a faixa em reprodução — creme contra o verde das demais — no lugar da barrinha de 3 px que só aparecia na margem esquerda.

**Medido depois:** na linha selecionada, 1508 px azuis e **zero** px pretos. **Verificado por mutação:** devolvido o texto não recortado, aparecem 123 px pretos e o teste reprova; recuada a barra, reprova também.

O medidor do limitador passou de 3 px para **1 px**, em âmbar contido: três linhas competiam com o espectro pela atenção, e o medidor é aviso, não instrumento principal.

### C-18 — "Ruídos ao tocar": o limitador trabalhando em todos os blocos, sem nada avisar

**Observado:** *"percebi uns ruídos no som ao tocar a música, será codec?"*

**Não era o codec.** Medido e descartado, junto com o resto do caminho:

| Hipótese | Medição | Resultado |
|---|---|---|
| Codec | Ruído em 16–21 kHz, WAV contra MP3 128k | Descartada — o LAME **corta** acima de 16 kHz; não há artefato ali para amplificar |
| Underruns | 20 s no dispositivo real | Zero. Blocos constantes de 826 quadros, posição colada no relógio |
| Decodificação e DSP | Renderização offline em WAV, MP3 e FLAC | Sem descontinuidade, sem clamp |
| Limitador defeituoso | Rajadas com ganho de entrada de 1× a 4× | Correto: ganho constante dentro da rajada (0,02 dB), teto mantido |
| Saída real do player | Captura por sink virtual, análise espectral | 0,00% de energia em frequências espúrias |

**A causa, medida:** a configuração do usuário tinha o equalizador com **preamp +5,08 dB**.

| Cenário | Pico após o EQ | Redução do limitador |
|---|---|---|
| EQ desligado | 0,67 | 0 dB, **0%** dos blocos |
| EQ do usuário | 1,39 | mediana **−2,3 dB**, pior −3,9 dB, **100%** dos blocos |
| Mesmo EQ, preamp 0 dB | 0,78 | 0 dB, **0%** dos blocos |

Não é distorção: é o ganho variando continuamente com o programa. A curva do EQ sozinha não causa nada — é o preamp que estoura o teto.

**O defeito real que isso expôs:** `AU-17` — *indicador de clipping na interface quando o limitador atua* — estava marcado **OK (M3)** e **não existia na interface**. Foi perdido quando os painéis foram reescritos para o formato de skin no M5, e o status nunca foi reconferido. O usuário ajustou o preamp, ganhou 15 dB de excesso e não tinha como saber.

**Correção:** medidor de redução do limitador na faixa livre entre os dígitos e o espectro (19,39, 76×3), alinhado com o espectro e travado no `test_layout`. Mostra *quanto*, e não só *se* — cor de amarelo a vermelho até 12 dB, que é a faixa do equalizador. Abaixo de meio decibel não acende: o limitador toca de leve no sinal o tempo todo, e um medidor que pisca por isso vira ruído visual em vez de aviso.

**Duas medições minhas que estavam erradas e foram corrigidas antes de virar conclusão:** medi a modulação do limitador sem alinhar o atraso de 1,5 ms do lookahead, o que dava 37 dB falsos de variação; e contei "saltos bruscos entre amostras" num sinal com 11 kHz, onde amostras vizinhas diferem muito por definição.

### C-19 — Playlist: seleção não acompanhava a faixa, e a barra de rolagem não respondia ao mouse

**Observado:** *"a música não fica selecionada na PLAYLIST quando troca de música"* e *"a barra de SCROLL não funciona clicando com o mouse e arrastando, somente com o botão SCROLL"*.

| Defeito | Causa | Correção |
|---|---|---|
| Seleção não acompanha | `refresh()` preservava a seleção e **nunca** olhava a faixa em reprodução. Pior: só era chamado quando chegava metadado, então a troca de faixa nem chegava ao painel | Ao mudar a faixa, ela vira a seleção e é trazida para a vista pelo mínimo necessário. Só na mudança — refazer a cada `refresh()` desfaria a seleção do usuário dez vezes por segundo |
| Barra de rolagem inerte | Ela era **desenhada** e não era área sensível: `mousePressEvent` não a testava | Clicar no cursor arrasta, clicar no trilho salta. Desenho e teste de acerto passaram a usar a **mesma** função de geometria — duas contas separadas divergem e o cursor deixa de pegar onde aparece |

Como o `refresh()` passou a ser chamado a cada tique, ele só repinta quando a assinatura do que se vê muda.

**Verificado por mutação** em `tests/test_playlist_ui.cpp`, que envia eventos de mouse ao painel: desligada a seleção que acompanha, 3 verificações falham; tornada a barra insensível, 2 falham.

### C-17 — A recuperação do dispositivo de áudio não funcionava, e o teste isolado não mostrava

A política de reabertura tinha teste e passava. Ao exercitá-la contra o sistema de áudio de verdade — criando um sink virtual com `pactl` e removendo-o com o player tocando nele — apareceram **dois defeitos que o teste isolado não podia ver**.

**1. O aviso do miniaudio não chega.** A detecção de perda dependia do `notificationCallback`. Medido: ao remover um sink do PulseAudio em uso, esse callback **não dispara**. Os quadros simplesmente param de ser puxados e o dispositivo continua se declarando saudável — observado por mais de 25 s sem nenhuma mudança de estado. O player ficaria mudo sem nada acusar.

*Correção:* o sinal de vida passou a ser o próprio callback de dados. Se o dispositivo está vivo, ele **puxa**. Um segundo sem nenhuma chamada, com o dispositivo supostamente tocando, é perda. O aviso do miniaudio continua ligado — em outros sistemas ele chega — mas deixou de ser a única detecção.

**2. Destruir o dispositivo morto trava.** `ma_device_uninit` num dispositivo cujo sink sumiu **não retorna**. Medido: 25 s de espera sem resposta, e nada indica que terminaria. Como a reabertura começava fechando o anterior, a recuperação inteira travava — e, na primeira versão, levava junto o thread da interface, congelando a janela.

*Correção:* o dispositivo perdido é **abandonado**, não destruído. A tentativa de liberá-lo segue num thread solto, que termina se o sistema de áudio voltar. O vazamento é deliberado e está comentado no código: acontece uma vez por perda de hardware, que é evento raro, e o preço de alguns descritores é menor que o de um player que nunca mais toca.

Para que o dispositivo abandonado não interferisse, cada abertura passou a ter um **canal próprio** com seu contador de quadros. Sem isso, o callback do morto continuaria incrementando o contador do vivo e mascararia a próxima parada.

**Verificado por mutação, com o sistema de áudio real:** desligado o detector de parada, o teste acusa "a perda do dispositivo foi detectada" e "a saída foi restabelecida"; trocado o abandono por destruição, o processo trava e o prazo do runner o reprova.

**Lição:** a política pura estava certa e continua certa — o erro estava em tudo que ela pressupunha sobre o mundo. Teste de unidade sobre uma abstração não verifica a abstração.

### C-16 — A diagramação passou a ser verificada, não ajustada no olho

**Observado:** *"TEMPO e FFT colado demais nos outros elementos. SLIDER do BALANÇO foi cortado no intuito de arranjar ou alinhar os elementos. Acho inacreditável não conseguir diagramar."*

A crítica é de método, e procede. Eu vinha corrigindo posição peça a peça contra a captura de tela, e cada correção soltava outra coisa: margem desigual, depois peças sobrepostas, depois vão zero. Encolher o balanço de 38 para 30 px para forçar o encaixe foi remendo — e visível.

**A conta que faltava:** com margem de 16 px o conteúdo tem 243 px, e a fileira de controles precisa de volume (68) + balanço (38) + par EQ|PL (46) = **152 px de peça mais os vãos**. Começando em 107, sobravam exatamente 0 px de vão. Não era questão de ajustar: o arranjo era impossível.

**A correção:** a janela passou a sair de duas medidas, anotadas em `winamp_layout.h`:

```
MARGEM = 14   conteudo de 14 a 260 nas tres janelas
VAO    >= 3   distancia minima entre pecas vizinhas
```

O 14 é o maior valor em que a fileira cabe com as larguras do formato — o balanço voltou aos 38 px.

**A ferramenta:** `tests/test_layout.cpp` foi reescrito. Ele ordena as peças de cada fileira, imprime a grade inteira com os vãos, e reprova margem menor que 14, vão menor que 3, sobreposição entre quaisquer duas peças, conteúdo não centrado no poço do mostrador e arestas que deveriam coincidir e não coincidem. A saída é o desenho da janela e a verificação ao mesmo tempo.

Ele já pagou na primeira execução: acusou `mono` e `estereo` sobrepostos em 2 px, defeito que nenhuma das rodadas anteriores tinha visto.

**Lição:** posição não se confere olhando. Enquanto a verificação foi visual, cada rodada trocava um defeito por outro.

### C-15 — Segunda comparação lado a lado: margens, relevo e tipografia

| Observação do usuário | Medição | Correção |
|---|---|---|
| *"tudo deslocado para a direita"* | Conteúdo começava em 16 e terminava em **264**: margem esquerda 16, direita **10** | Tudo que fica à direita recuou para 258. As duas janelas agora têm 16 px de cada lado, verificado por varredura: título, mono/estéreo, volume, EQ/PL, barra de posição e logotipo terminam todos em 258 |
| *"o mesmo ocorre na tela EQ"* | Preamp em 21, bandas até 262, rótulo `PREAMP` começando em 12 | Preamp em 20, bandas de 70 a 254, traços de escala fechando em 258, `PREAMP` alinhado à esquerda em 16 |
| *"todos os SLIDERS estão saltados… no sentido da iluminação contrária"* | `color_bar` punha o realce na linha de cima e a sombra na de baixo — relevo de peça **saliente** | Invertido: sombra em cima, luz embaixo; nas barras verticais, sombra à esquerda e luz à direita. Agora lê como encaixe |
| *"botões da playlist diferentes dos do player"* | Playlist: retângulo chapado com um fio de contorno. Player: face em degradê com relevo | Playlist passou a usar o mesmo degradê e o mesmo relevo |
| *"pontos do painel viram traços e desalinham"* | A grade gravada no `main.bmp` começa em linha par; a desenhada em tempo de execução partia de `kVisualization.y() = 43`, **ímpar** — meio passo fora | A grade de execução passou a contar em coordenada absoluta par. Os dois conjuntos coincidem |
| *"texto DB desalinhado"* | O glifo `+` ocupava as 5 colunas da célula, sem vão, e encostava no `1` | `+` reduzido a 4 colunas. Os rótulos gravados no bitmap ganharam **avanço proporcional** (tinta + 1), que também resolve `AMP` colado |
| *"relógio da playlist flat"* | Campo preto sem relevo | Ganhou moldura embutida, como os demais mostradores |
| *"STEREO desalinhado com o fim dos elementos"* | Tinta terminava em 255 | Alinhado à direita dentro da célula, termina em 258 |

**Divergência deliberada registrada:** as margens simétricas de 16 px não são as do formato, que dá 16 à esquerda e 10 à direita. O desequilíbrio era visível e a decisão foi do usuário; está anotado em `winamp_layout.h` junto das constantes.

### C-14 — Sete defeitos de alinhamento na primeira comparação lado a lado

Todos medidos no nosso próprio render, não estimados.

| Observação do usuário | Medição | Correção |
|---|---|---|
| *"tempo + fft fora de centro"* | Conteúdo do mostrador em x 24..99 (centro 61,5) dentro de um poço de 9..103 (centro 56): **15 px de preto morto à esquerda contra 4 à direita** | Poço redimensionado para 15..106. O interior passa a ter centro 60,5, e a aresta esquerda cai em 16 — a mesma do transporte e da barra de posição |
| *"desalinhado com o volume"* | Poço ia até 103; o volume começa em 107 | Poço termina em 106, encostado no volume |
| *"logo do raio colada na lateral direita"* | Disco em 255..274; a borda da janela é 274 | Movido para 245..264, onde a aresta direita bate com a do poço do título e a do botão `PL` |
| *"nome da música cortado pelo fim da janela"* | Poço do título ia de 109 a 274, e o texto até 263 | Poço 107..264, campo de texto 111..260, 3 px de folga de cada lado |
| *"botão de volume e balanço desalinhado verticalmente com a barra"* | Barra nas linhas 4..10 da célula (centro 7), polegar nas linhas 1..11 (centro 6) — **1,5 px de desvio** | Barra nas linhas 3..9, centro 6. Os dois agora na linha 63 da janela |
| *"caption da playlist diferente da EQ diferente da principal"* | Barra da playlist com 20 px de altura e **seis** listras; as outras com 14 px e três | Playlist passou a 14 px e três listras, com o mesmo bloco e o mesmo creme |
| *"caption escrito PAYAMPING"* | Os glifos estavam certos: `PLAYAMPNG` em caixa alta, sem separador, não se lê | Escrito `PLAYAMP NG` nas três janelas e no título da faixa |

Também nesta rodada: a barra de posição ganhou um canal de fato recuado — o fundo estava a 72% da cor da face e lia-se como risco flutuando, não como encaixe; e as verticais da grade da curva do equalizador passaram a cair sobre as bandas em vez de seguir passo próprio.

**O que o teste pegou sozinho:** ao encostar o poço do mostrador em 107, a borda dele caiu dentro do controle de volume, e `test_skin` reprovou por fundo não-liso sob o volume. Foi o que definiu o recuo para 106.

### C-13 — A aparência não era a do Winamp porque a referência nunca foi lida

**Observado:** *"Te envie a tela do WINAMP, nada foi implementado."* e *"Não tem FIM os problemas visuais."*

**Causa:** a imagem de referência saiu do meu contexto numa compactação e eu não fui buscá-la — ela estava em disco o tempo todo, em `~/.claude/image-cache/`. Sem ela, cada rodada de ajuste era palpite, e palpite corrigia um ponto e quebrava outro. Foi isso que produziu o ciclo de defeitos novos a cada entrega.

**Correção:** a imagem foi lida e medida. A paleta inteira passou a sair de amostragem dela (`APROXIMACOES.md`), e os elementos que faltavam foram implementados contra o que ela mostra: barras finas sem poço, cursores claros, botões de transporte de face clara, botões de alternância com LED, escala de dB do equalizador, grade da curva, grade pontilhada da visualização, listras cremes na barra de título.

**Lição registrada em memória:** quando o usuário fornece referência visual, implementar por medição dela; e não refazer decisão visual que ele já validou sem perguntar antes — as duas coisas que produziram este episódio.

### C-12 — A arte regrediu ao adotar o formato

**Observado:** *"Degradou muito em relação ao que estava. FFT+TEMPO sobreescrevem outros componentes. No tempo o PAUSE quebrou o canto da JANELA. VOLUME, BALANÇO e BARRAS do equalizador ficaram com um fundo errado e parcial, sumiram os captions do equalizador."*

Cinco defeitos distintos, todos no gerador de arte, todos medidos:

| O que estava errado | Medição | Correção |
|---|---|---|
| **Poços cruzados no mostrador** | `well(24,24,76,36)` e `well(33,23,74,17)` se sobrepunham; o preto ia de 24 a 106 em x e de 23 a 59 em y, com bordas internas no meio | Um poço só, `well(23,24,78,36)`. Tempo, indicador e visualização dividem a mesma área preta — o "bloco TEMPO + FFT" |
| **O indicador quebrava o canto** | O indicador ocupa 26..34; a borda esquerda do poço do tempo caía em 33, dois pixels em cima dele | Sem borda interna, não há canto para quebrar |
| **Moldura dupla em volume, balanço e posição** | `main.bmp` desenhava um poço e o `volume.bmp`/`balance.bmp`/`posbar.bmp` desenhava outro por cima | No formato o quadro do controle já é o fundo inteiro. As molduras saíram do `main.bmp` |
| **Preenchimento parcial nas barras** | Erro meu: eu tinha trocado o preenchimento cheio por um parcial, e o resto da barra virava um buraco preto | Revertido. A barra inteira muda de cor e o polegar diz o valor, como você havia descrito |
| **Caixas pretas atrás de cada rótulo** | O texto sai de `text.bmp`, que tem fundo preto. Desenhado sobre o cinza, cada rótulo carregava junto uma caixa que parecia sujeira | Os campos numéricos, os rótulos do equalizador e os botões da playlist passaram a ser pretos de propósito — a caixa vira o campo |

Também voltaram as marcas de 0 dB em cada slider do equalizador e os rótulos de banda (`PREAMP`, `60`…`16K`).

**Teste:** `test_skin` passou a exigir que o `main.bmp` seja liso sob volume, balanço e barra de posição. Verificado com mutação: repõe-se um dos poços e ele acusa a falha pelos dois caminhos de carga (pasta e `.wsz`).

### C-7 — A interface não tinha as proporções do original

**Observado:** *"NOVAMENTE BARRAS DESALINHADAS COM ESPAÇAMENTO IRREGULAR. Não centralizado."* e *"Perceba o alinhamento do FFT com o elemento barra de volume. Não alinha com nenhum outro elemento."*

**Medido:** o painel principal tinha **21 bordas esquerdas distintas, 21 direitas e 12 topos** — nenhum controle compartilhava aresta com outro.

**Causa:** as coordenadas eram cálculos meus para caber em 275 × 116. Cada correção de um elemento soltava outro, porque não havia relação entre eles, só valores independentes.

**Correção:** adotado o formato de skin do Winamp 2.x como especificação de geometria (`src/ui/skin/winamp_layout.h`), com carregador de `.wsz` (`zip.cpp`, `winamp_skin.cpp`) e arte própria gerada no mesmo formato (`tools/make_wsz.py`). O `tests/test_layout.cpp` deixou de afirmar coordenadas e passou a afirmar **relações** — mesma faixa, passo constante, dentro da janela. Verificado com mutação: o teste passa intacto e reprova cinco alterações distintas (passo do transporte, balanço invadindo o volume, dígitos encostando, bandas fora da faixa do preamp, botões separados).

### C-8 — Os 28 fundos de slider do equalizador não existiam

**Observado:** os cursores do equalizador apareciam flutuando, sem trilho atrás.

**Medido:** `eqmain.bmp` tem 315 px de altura; empilhar 28 quadros de 63 px a partir de y=164 exigiria 1928. Só dois quadros cabiam, e os outros 26 vinham vazios.

**Causa:** no formato os 28 quadros estão em **duas fileiras de catorze** — (13,164) e (13,229) —, não empilhados na vertical.

**Correção:** `winamp::eq_slider_frame(i)` devolve o recorte da grade. O `test_skin` passou a percorrer os 28 quadros de slider e os 28 de volume e balanço em vez de conferir só o primeiro; com a versão errada ele acusa 52 falhas.

### C-9 — A fonte saía com forma de letra errada

**Observado:** `PLAYAMPNG` aparecia como `PLRYRMPNG`.

**Medido:** o glifo do `A` era `.##.. / #..#. / ####. / #..#. / #..#.` — travessão de 4 px, faltando a coluna da direita, o que o faz ler como `R`.

**Causa:** tabela de glifos escrita à mão, pela terceira vez. A validação que eu havia acrescentado conferia contagem de coluna, e contagem de coluna estava certa. Forma de letra não dá para validar por programa.

**Correção:** a tabela deixou de ser desenhada. `tools/fontgen.cpp` extrai os glifos da **Silkscreen** em 8 px e falha se qualquer glifo perder tinta fora da célula de 5 × 6; a saída é colada em `tools/make_wsz.py`. Só `&` e `$` perdem a linha de baixo, por terem 7 px de altura, e isso está em `APROXIMACOES.md`.

### C-10 — Os controles deslizantes não mostravam o valor

**Observado:** *"Perceba que as barras volume e equalizador mudam de cor e não tem o degrade de cores no fundo."*

**Medido:** os 28 quadros preenchiam a barra inteira e só mudavam de matiz; volume no mínimo e no máximo ficavam do mesmo tamanho.

**Correção:** o quadro passa a dizer o nível por dois sinais, como no formato: comprimento do preenchimento e cor. Sem degradê de fundo. No balanço o preenchimento cresce do centro para a ponta.

### C-11 — Texto estourando as células da arte

**Medido:** `stereo` mede exatamente 29 px de tinta (6 glifos de 5 px, o último sem o vão) e era desenhado a partir de x=31 numa célula de 29 — as duas últimas colunas do `O` sumiam. O campo de bitrate do formato tem 15 px, o bastante para `320`; arquivo sem perda informa `1411`, que ocupa 20 px e encostava no rótulo `kbps`.

**Correção:** `stereo` começa em x=29 e o rótulo `kbps` foi para x=132. Medido depois: os dois legíveis, sem corte e sem encosto.

### C-3 a C-6 — observações do segundo teste manual

| # | Observação | Causa | Correção |
|---|---|---|---|
| C-3 | **`W` e `Ctrl+1/2/3` não funcionavam** | As teclas eram tratadas no painel principal, mas o evento vai para o widget **com foco**. Com a playlist focada, `Ctrl+1` não era texto imprimível, caía no tratador padrão e morria. Pior: `W` sozinho era engolido pela busca por digitação da playlist. | Viraram `QShortcut` com `Qt::ApplicationShortcut`, que dispara independente do foco. `W` virou `Ctrl+W`, mais duplo clique na barra de título — o gesto clássico. **Verificado medindo a janela:** `Ctrl+3` → 825×798, `Ctrl+1` → 275×266, `Ctrl+E` → 550×764, `Ctrl+W` → 550×28. |
| C-4 | **Sem como separar os painéis** | O modo integrado foi implementado sem o destacado. | `Ctrl+D` alterna. No Wayland o compositor posiciona e não há encaixe — está documentado e é o comportamento previsto. |
| C-5 | **Sem como aumentar a playlist** | O painel tinha altura fixa. | Arrastar a aresta inferior redimensiona; o cursor muda ao passar por cima; a altura persiste. |
| C-6 | **Texto saindo das caixas** | O corte do título dividia a largura disponível pela largura do glifo e ainda deixava encostar na duração. O total passava de uma hora e saía `620:25`. | O corte reserva 8 px de folga e marca a supressão com `.`; o total ganha o dígito de hora: `10:20:25`. |

Os quatro apareceram só em uso real. `C-3` em particular é do tipo que nenhum teste automatizado meu pegaria: o código estava correto, o **foco** é que nunca chegava nele.

### C-1 — O clamp rígido do limitador acendia o indicador de clipping sem falha real

**Observado:** indicador `CLIP` aceso durante reprodução normal com o preset Rock, na captura do usuário.

**Medido:**

```
EQ Rock: clamp_hits=6  pico=0.8913 (teto 0.8913)
clamp #0 amostra  371872: atrasada=1.03541 ganho=0.860770 alvo=0.860770 excesso=5.96e-08
clamp #2 amostra 1050619: atrasada=1.25122 ganho=0.712306 alvo=0.712306 excesso=5.96e-08
```

**Causa:** não era falha do limitador. Em todos os seis casos o ganho havia alcançado o alvo com precisão (`ganho == alvo`), e o excesso era de **5,96e-08 — um ULP de float32**. A conta `ceiling/pico × pico` não devolve `ceiling` exato em ponto flutuante, e o contador registrava esse último bit como "o lookahead falhou".

**Correção:** o clamp continua valendo bit a bit — a garantia de saída não mudou, e o pico medido continua exatamente no teto. O que mudou foi o limiar de **contagem**: agora só conta atuação acima de `teto × (1 + 1e-6)`, cerca de 1e-5 dB. Depois: `clamp_hits=0`.

**Lição:** um contador de diagnóstico precisa de tolerância numérica tanto quanto uma asserção de teste. Eu havia escrito no relatório do M3 que "um valor diferente de zero aqui é falha, não estatística" — a afirmação continua válida, mas só depois de o contador medir falha em vez de arredondamento.

### C-2 — Descartes de visualização acumulavam durante a pausa

**Observado:** `vis descartados: 2769` na captura do usuário, com o player **pausado**.

**Medido:** `pausado, sem consumir: vis_drops 34 -> 534` em meio segundo.

**Causa:** em pausa o consumidor para de ler o ring de propósito, para congelar o último quadro (VI-15). Mas o thread de áudio continuava capturando silêncio, enchia o ring e incrementava o contador de descartes. Nada de errado acontecia, e o diagnóstico dizia que sim.

**Correção:** só captura em estado `Playing`. Depois: contador estável.
