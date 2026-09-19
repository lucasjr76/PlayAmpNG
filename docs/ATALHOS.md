# PlayAmpNG — Atalhos de teclado

IN-01. Esta lista também está **dentro do programa**: botão direito na janela principal → *Atalhos de teclado*. Documentar só aqui não ajuda quem já abriu o player e não sabe o que apertar.

Os atalhos são de aplicação, não de widget: funcionam com qualquer painel focado. A primeira versão tratava as teclas no painel principal e elas só respondiam se *ele* estivesse com o foco — com a playlist focada, nada acontecia (`DEFEITOS.md`, C-3).

## Reprodução

| Tecla | Ação |
|---|---|
| `Espaço` | tocar / pausar |
| `Z` | faixa anterior |
| `X` | tocar |
| `C` | pausar |
| `V` | parar |
| `B` | próxima faixa |
| `←` `→` | retroceder e avançar 5 s |
| `↑` `↓` | volume |

## Janela

| Tecla | Ação |
|---|---|
| `Ctrl+1` `Ctrl+2` `Ctrl+3` | escala 1×, 2×, 3× |
| `Ctrl+W` | modo barra (também: duplo clique na barra de título) |
| `Ctrl+D` | separar ou juntar os painéis |
| `Ctrl+E` | equalizador |
| `Ctrl+P` | playlist |
| `Ctrl+T` | manter acima das demais janelas |

`Ctrl+W` e não `W`: a tecla sozinha era engolida pela busca por digitação da playlist.

## Playlist

| Tecla | Ação |
|---|---|
| `Delete` | remover a seleção |
| `Enter` | tocar a seleção |
| `↑` `↓` `PgUp` `PgDn` | rolar |
| digitar | busca por prefixo (reinicia após 1 s de pausa) |
| `Ctrl+clique` | alternar item na seleção |
| `Shift+clique` | selecionar intervalo |

## Teclas de mídia

As teclas de mídia do teclado funcionam **sem foco na janela**. No Linux elas chegam pelo MPRIS: o ambiente de trabalho as encaminha ao player registrado no barramento (IN-04, IN-05). Um programa que capturasse a tecla por conta própria brigaria com os outros players e só funcionaria com a janela em foco.

O mesmo caminho aceita comandos de qualquer cliente MPRIS — `playerctl`, o applet de mídia do ambiente, ou os controles da tela de bloqueio.

## Mouse

| Gesto | Ação |
|---|---|
| Botão direito na janela principal | menu: dispositivo de saída, propriedades da faixa, atalhos |
| Duplo clique na barra de título | modo barra |
| Clique no mostrador de tempo | alterna decorrido / restante |
| Clique na visualização | alterna espectro / osciloscópio / desligado |
| Arrastar a aresta inferior da playlist | redimensiona |
| Arrastar arquivos para a janela | adiciona à playlist |

Passar o mouse sobre qualquer controle mostra o nome dele e o atalho correspondente (IN-03).
