# PlayAmpNG — Aproximações visuais

A especificação (§1) pede que, na ausência de uma referência disponível, as aproximações adotadas sejam documentadas. Este arquivo é essa lista.

## Por que existe uma aproximação

Os bitmaps do skin base do Winamp são material protegido por direitos autorais e **não são redistribuídos**. O §2 da especificação também proíbe suporte a skins, o que descarta carregar um `.wsz` do usuário. Logo, o atlas é redesenhado do zero seguindo a linguagem visual descrita — não copiado.

O atlas é gerado por `tools/make_skin.py`, e a arte vive em código: qualquer ajuste de paleta ou de glifo é um diff legível, e o PNG é reproduzível a partir da fonte. `assets/skin/atlas.png` tem 3,5 KB e 130 sprites.

## O que é fiel

| Item | Valor |
|---|---|
| Dimensão do painel principal | 275 × 116 px, como o clássico |
| Densidade e organização | Mostrador de tempo à esquerda, visualização ao lado, transporte embaixo |
| Linguagem visual | Cinza escuro, bordas chanfradas com luz no topo-esquerda, mostradores e texto em verde |
| Estados de botão | Normal, pressionado, ativo, desabilitado e foco — os cinco exigidos |
| Barra de título desenhada pelo aplicativo | Sem moldura do sistema, como no original |
| Tipografia bitmap | Fonte própria de 5 × 7, maiúsculas, minúsculas mapeadas para maiúsculas |
| Mostrador de tempo | Dígitos próprios de 9 × 13, em sete segmentos |

## O que diverge, e por quê

| Divergência | Razão |
|---|---|
| **Coordenadas exatas dos controles** são nossas | Sem a referência original disponível, as posições foram escolhidas para caber em 275 × 116 com a mesma densidade. As proporções e a leitura são equivalentes; os pixels, não. |
| **Paleta** é própria | Verde `#00FF7F` sobre cinza `#2A2A2A`, com chanfros em `#606060` e `#101010`. Escolhida para o mesmo contraste do original, não amostrada dele. |
| **Desenho dos glifos** é próprio | A fonte do Winamp é um bitmap protegido. A nossa tem a mesma altura de 7 px e o mesmo espírito compacto. |
| **Espectro com 19 barras, verde a amarelo** | O número de barras é o do clássico; a rampa de cor é nossa. |
| **Apenas escala inteira** (1×, 2×, 3×) | Escala fracionária borraria pixel art e desalinharia os mostradores de sete segmentos. O preço é não haver ajuste fino de tamanho. |
| **1× é praticamente inutilizável em 4K** | 275 × 116 px num monitor de alta densidade é um selo postal. O padrão é 2×. O 1× existe como caso de teste de fidelidade, não como modo de uso. |
| **Presets do equalizador têm curvas próprias** | Os valores exatos dos presets originais nunca foram publicados como especificação. As nossas curvas estão em `src/core/dsp/presets.cpp` e foram conferidas com os testes de resposta. |

## Pendências conhecidas de aparência

- Os painéis de **equalizador e playlist** ainda usam widgets Qt estilizados em vez de sprites. Só o painel principal está desenhado com o atlas.
- **Modo compacto** (barra) não implementado.
- **Encaixe magnético entre janelas destacadas** indisponível no Wayland, por restrição do protocolo — ver `ARCHITECTURE.md §8`.
- Em compositor de modo *tiling*, a janela precisa da dica de flutuante. O aplicativo já a envia (`Qt::Dialog`), mas a decisão final é do compositor; no Hyprland pode ser necessária uma regra de janela.
