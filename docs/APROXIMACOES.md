# PlayAmpNG — Aproximações visuais

A especificação (§1) pede que, na ausência de uma referência disponível, as aproximações adotadas sejam documentadas. Este arquivo é essa lista.

## Por que existe uma aproximação

Os bitmaps do skin base do Winamp são material protegido por direitos autorais e **não são redistribuídos**. O que foi adotado é o **formato** de skin do Winamp 2.x — a geometria, não a arte: quais peças existem, onde cada uma mora dentro de cada bitmap e onde cada uma é desenhada dentro da janela. Essa tabela está em `src/ui/skin/winamp_layout.h` e é tratada como especificação.

A arte que acompanha o projeto é desenhada do zero por `tools/make_wsz.py`, que produz os doze bitmaps do formato mais o `viscolor.txt`, tanto como pasta quanto como `.wsz`. A arte vive em código: qualquer ajuste de paleta é um diff legível e os bitmaps são reproduzíveis a partir da fonte. `assets/skin/default.wsz` tem 9,5 KB.

**Mudança em relação ao plano original.** O §2 da especificação dispensa suporte a skins, e a primeira versão seguiu isso com um atlas de sprites e coordenadas próprias. O resultado não tinha as proporções do original — não porque os controles estivessem em posições absurdas, mas porque nenhum deles compartilhava aresta com outro. Adotar as coordenadas do formato resolveu isso de uma vez, e carregar `.wsz` saiu de graça junto: o carregador é o mesmo caminho que lê a arte própria. Dispensar suporte a skins continua valendo como "não é requisito"; o que existe agora é um efeito colateral de usar o formato como especificação.

## A paleta é medida, não escolhida

Toda cor do skin foi lida da captura de referência do Winamp fornecida pelo usuário, por amostragem de área lisa e por busca de pixel saturado. As anteriores eram cinza e verde-neon, inventadas — e eram a maior diferença visível que sobrava depois de a geometria já estar certa.

| Papel | Valor | Onde foi lido |
|---|---|---|
| Face do painel | `(58, 58, 86)` | cor mais frequente da imagem inteira |
| Fundo dos mostradores | `(0, 0, 0)` | interior da área de visualização |
| Texto do mostrador (LCD) | `(145, 201, 98)` | título da faixa |
| Listras e rótulos da barra de título | `(252, 251, 233)` | listras da barra |
| Face dos botões de transporte | `(214, 222, 230)` topo, `(166, 174, 190)` base | centro dos botões |
| Barra no mínimo | `(84, 142, 38)` | barra de balanço centrada |
| Barra no meio | `(217, 203, 71)` | barra de volume e preamp |
| Barra no alto | `(195, 117, 49)` | sliders realçados do equalizador |
| Rótulos de banda | `(211, 211, 221)` | `PREAMP` |
| Rótulos de dB | `(217, 203, 71)` | `+12 db` |

O degradê de valor das barras é a interpolação entre esses três extremos medidos, e é a mesma tabela usada pelo volume, pelo balanço, pelos sliders do equalizador e pelo espectro — uma família de cor só na janela inteira.

## A geometria também é medida

Calibrei a captura pelos cinco botões de transporte, cuja posição o formato fixa: passo de 59,5 px de imagem para 23 lógicos dá escala **2,587**, origem x **1,02** e y **−9,2**. Com isso dá para ler qualquer elemento da referência em coordenadas lógicas e comparar com as nossas.

| Elemento | Referência | O que estava aqui | Erro |
|---|---|---|---|
| Poço do mostrador | x 9..103, y 22..64 | x 24..99, y 25..58 | menor em cada lado; a diferença de proporção mais visível |
| Poço do título | x 110..273, y 24..35 | x 108..265, y 25..35 | 8 px curto à direita |
| Campo de bitrate | x 110..128, y 42..51 | x 110..131, y 42..49 | 3 px largo, 2 px baixo |
| Campo de taxa | x 157..170, y 42..51 | x 155..166, y 42..49 | deslocado 2 px |
| Barra de valor | 5 px de cor + contorno escuro | 7 px chapados | gorda demais |
| Barra de posição | canal recuado de 10 px | fio de 1 px no meio da face | lia-se como risco, não como encaixe |
| Listras da barra de título | 3, entre as linhas 4 e 9 | 4, entre 3 e 10 | — |
| Passo das bandas do EQ | 18,9 (centros de 84 a 254) | 18 | a décima banda caía 8 px à esquerda |
| Barra do slider do EQ | 5 px | 7 px | — |
| Grade do mostrador | cobre o poço inteiro | só o retângulo do espectro | metade do bloco ficava chapada |

Também faltavam, e foram acrescentados: o disco do logotipo no canto inferior direito (20 × 20 em 255,90) e o raio na ponta esquerda da barra de título.

## Texto sem caixa

A referência não tem caixa escura atrás de rótulo nenhum. O `text.bmp` do formato tem fundo preto, então desenhar um rótulo sobre a face levava a caixa junto — e a solução anterior era tornar cada campo preto de propósito, o que só disfarçava.

`WinampSkin::draw_text` ganhou uma variante que recorta o glifo e o pinta na cor pedida. A cor de fundo não é fixada em preto: sai da célula do **espaço**, que por definição não tem desenho, de modo que um `.wsz` de terceiros com `text.bmp` sobre outra cor continua recortando certo. Os campos que continuam pretos — título da faixa, bitrate, taxa, total da playlist — são pretos porque **na referência eles são**, não por contorno do bitmap.

## O que a referência ditou, além da cor

| Elemento | O que a referência mostra |
|---|---|
| Barras de volume, balanço e equalizador | Barra fina de pontas arredondadas assentada direto na face, **sem poço atrás**. A barra inteira muda de cor; quem diz o valor é a posição do cursor |
| Cursores | Bloco claro com contorno escuro de 1 px e marcas de pega — o mesmo bloco em todos os controles |
| Botões de transporte | Face **clara** com símbolo escuro, não face do painel |
| Botões de alternância | Face do painel com borda clara, LED de estado e rótulo creme — tipo distinto do transporte |
| Barra de título | Listras cremes com bloco liso atrás do nome; inativa, as listras escurecem em vez de sumir |
| Equalizador | Escala `+12db` / `+0db` / `-12db` com traços ladeando cada slider, rótulos de banda sob eles, e grade no mostrador da curva |
| Campos de texto | Todo texto do formato sai de `text.bmp`, que tem fundo preto: os campos numéricos e os rótulos de botão são pretos de propósito |

## O que é fiel
## O que diverge, e por quê

| Divergência | Razão |
|---|---|
| `&` e `$` perdem a linha de baixo | Os dois têm 7 px de altura na Silkscreen e a célula do formato tem 6. Nenhum outro glifo perde tinta — `fontgen` falha se perder. |
| `Å`, `Ö` e `Ä` saem em branco | Os diacríticos exigem 8 px de altura numa célula de 6. São a terceira fileira do `text.bmp` e não aparecem em nenhum texto do player. |
| A curva do equalizador não se alinha aos sliders | No formato ela é um mostrador próprio de 113 px contra os 176 das dez bandas, e fica acima dos controles. Não é desalinhamento: é assim no original. |
| **Paleta** é própria | Verde `#00ED00` sobre cinza `#3A3A3A`, poços em preto puro, chanfros em `#626262` e `#1A1A1A`. A primeira versão usava verde-primavera `#00FF7F`, que puxa para o azul; o mostrador clássico é verde puro, e a diferença salta aos olhos na tela ainda que suma numa captura pequena. |
| **Trilhos coloridos pelo valor** | Volume, balanço e cada banda do equalizador são faixas de **cor sólida**, e a cor diz o valor: verde em nível baixo ou corte, amarelo perto do plano, laranja/vermelho em nível alto ou reforço. A versão anterior pintava um degradê de arco-íris fixo no fundo e "preenchia" até o valor — leitura oposta à do original. |
| **Tempo e visualização num bloco só** | Eram dois poços separados, com bordas diferentes. No original formam um bloco preto contínuo, e é isso que dá a leitura de mostrador único em vez de duas caixinhas. |
| **Degradê do espectro** é próprio | Verde na base, amarelo no meio, vermelho no pico, em 16 passos. A progressão perceptiva segue a referência; os valores RGB são nossos. A primeira versão variava o matiz por **barra**, produzindo um arco-íris horizontal que o Winamp nunca teve — o clássico varia a cor com a **altura**. |
| **Rótulos das bandas do equalizador** | O Winamp não rotula as bandas. Mantivemos os rótulos por legibilidade, o que obrigou a alargar o passo de 18 para 20 px. |
| **A fonte não é a do Winamp** | A do Winamp é um bitmap protegido. A Silkscreen é uma fonte de pixel independente, de licença aberta, com proporções próximas. |
| **Espectro com 19 barras, verde a amarelo** | O número de barras é o do clássico; a rampa de cor é nossa. |
| **Apenas escala inteira** (1×, 2×, 3×) | Escala fracionária borraria pixel art e desalinharia os mostradores de sete segmentos. O preço é não haver ajuste fino de tamanho. |
| **1× é praticamente inutilizável em 4K** | 275 × 116 px num monitor de alta densidade é um selo postal. O padrão é 2×. O 1× existe como caso de teste de fidelidade, não como modo de uso. |
| **Presets do equalizador têm curvas próprias** | Os valores exatos dos presets originais nunca foram publicados como especificação. As nossas curvas estão em `src/core/dsp/presets.cpp` e foram conferidas com os testes de resposta. |

## Pendências conhecidas de aparência

- O acabamento ainda não está no nível do original: espaçamentos internos, proporção dos botões e tratamento das superfícies pedem mais uma passada.
- Em compositor que aplica opacidade a janelas sem foco — o caso do Hyprland nesta máquina — o conteúdo atrás aparece através do painel. É configuração do ambiente, não do aplicativo.
- **Encaixe magnético entre janelas destacadas** indisponível no Wayland, por restrição do protocolo — ver `ARCHITECTURE.md §8`.
- Em compositor de modo *tiling*, a janela precisa da dica de flutuante. O aplicativo já a envia (`Qt::Dialog`), mas a decisão final é do compositor; no Hyprland pode ser necessária uma regra de janela.

## Modo compacto e divergências do formato de skin

O modo compacto (AP-11) segue as coordenadas do *windowshade* do Winamp 2.x, conferidas na implementação de referência Webamp (`css/main-window.css` e `js/skinSprites.ts`), e não de memória. A razão é concreta: nos skins Winamp os mini-controles vêm desenhados dentro da imagem de fundo, e as áreas de clique só acertam se estiverem onde o skin os desenhou.

| Peça | Posição | Origem |
|---|---|---|
| mini-controles ⏮ ▶ ⏸ ⏹ ⏭ ⏏ | 169–224 | formato |
| tempo `MM:SS` | 127 | formato |
| barra de posição | 226–242 | formato |
| minimizar, expandir, fechar | 244, 254, 264 | formato, **só no compacto** |
| título da faixa | 20–119 | **nosso** — o Winamp não mostra título no compacto; aqui ele ocupa o lugar da mini-visualização |

No modo normal os botões da janela ficam em 232/242/252, na grade do projeto (decisão já aceita). No compacto eles precisam ir para as posições do formato, porque a barra de posição ocupa 226–242 e colidiria com o minimizar em 232.

Duas divergências do formato encontradas no `titlebar.bmp`, **sem efeito com o skin próprio**, registradas porque importariam se o player um dia carregar skins de terceiros:

- a barra com e sem foco estão invertidas: aqui, sem foco em y=0 e com foco em y=15; no formato, o contrário;
- o botão "compacto" ocupa a célula (0,0), que no formato é o botão de opções; no formato ele fica em (0,18).
