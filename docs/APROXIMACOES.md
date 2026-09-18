# PlayAmpNG — Aproximações visuais

A especificação (§1) pede que, na ausência de uma referência disponível, as aproximações adotadas sejam documentadas. Este arquivo é essa lista.

## Por que existe uma aproximação

Os bitmaps do skin base do Winamp são material protegido por direitos autorais e **não são redistribuídos**. O §2 da especificação também proíbe suporte a skins, o que descarta carregar um `.wsz` do usuário. Logo, o atlas é redesenhado do zero seguindo a linguagem visual descrita — não copiado.

O atlas é gerado por `tools/make_skin.py`, e a arte vive em código: qualquer ajuste de paleta ou de glifo é um diff legível, e o PNG é reproduzível a partir da fonte. `assets/skin/atlas.png` tem 3,5 KB e 130 sprites.

## O que é fiel

| Item | Valor |
|---|---|
| Dimensão do painel principal | 275 × 116 px, como o clássico |
| **Coordenadas dos controles** | As do Winamp Classic: tempo em 36,26; visualização em 24,43 (76 × 16); título em 111; volume em 107,57 (68 × 13); balanço em 177,57 (38 × 13); EQ/PL em 219 e 242; barra de posição em 16,72 (248 × 10); transporte em 16,88 com botões de 23 × 18; eject 22 × 16; shuffle 46 × 15; repeat 28 × 15 |
| Botões da barra de título | Minimizar, compactar e fechar, 9 × 9, em 244/254/264 |
| Densidade e organização | Mostrador de tempo à esquerda, visualização ao lado, transporte embaixo |
| Linguagem visual | Cinza escuro, bordas chanfradas com luz no topo-esquerda, mostradores e texto em verde |
| Estados de botão | Normal, pressionado, ativo, desabilitado e foco — os cinco exigidos |
| Barra de título desenhada pelo aplicativo | Sem moldura do sistema, como no original |
| Tipografia | **Silkscreen** (SIL OFL), fonte de pixel desenhada para 8 px. Rasterizada uma vez na carga, em 1×, sem suavização, para um atlas de texto |
| Mostrador de tempo | Dígitos próprios de 9 × 14, em sete segmentos **com os segmentos separados** — barras verticais recuadas duas linhas em relação às horizontais, de modo que cada segmento se lê como peça solta |
| Relevo dos controles | Face em degradê vertical mais duas arestas (contorno e realce interno). Uma face chapada com uma linha de contorno lê-se como retângulo desenhado, não como volume |

## O que diverge, e por quê

| Divergência | Razão |
|---|---|
| **Coordenadas exatas dos controles** são nossas | Sem a referência original disponível, as posições foram escolhidas para caber em 275 × 116 com a mesma densidade. As proporções e a leitura são equivalentes; os pixels, não. |
| **Paleta** é própria | Verde `#00ED00` sobre cinza `#3A3A3A`, poços em preto puro, chanfros em `#626262` e `#1A1A1A`. A primeira versão usava verde-primavera `#00FF7F`, que puxa para o azul; o mostrador clássico é verde puro, e a diferença salta aos olhos na tela ainda que suma numa captura pequena. |
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
