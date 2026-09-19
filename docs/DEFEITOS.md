# PlayAmpNG — Defeitos e observações de teste manual

Lista viva. Cada item diz quem observou, o que foi medido e onde foi corrigido. Itens abertos ficam no topo.

---

## Abertos

| # | Observação | Origem | Situação |
|---|---|---|---|
| A-11 | Encaixe magnético entre painéis destacados | — | Indisponível no Wayland por restrição de protocolo (`ARCHITECTURE.md §8`) |
| A-12 | `LI-04` — reordenar a playlist arrastando | Regressão consciente do M5 | A reimplementar no painel em sprites |

---

## Corrigidos

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
