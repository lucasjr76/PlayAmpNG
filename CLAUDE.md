# PlayAmpNG — guia para sessões de trabalho

Player de áudio desktop inspirado no Winamp Classic. C++20, Qt 6, FFmpeg, miniaudio. Linux, Windows e macOS.

Tudo em **português**: código comentado, documentação, mensagens de commit. Comentários explicam o **porquê**, e registram o defeito que motivou a decisão quando houve um.

## Onde está cada coisa

| | |
|---|---|
| Especificação original | `BASIC_SPEC.txt` |
| Requisitos e estado de cada um | `docs/REQUIREMENTS.md` — **fonte da verdade do progresso** |
| Arquitetura e decisões | `docs/ARCHITECTURE.md` |
| Plano por marcos | `docs/PLAN.md` |
| Resultados medidos | `docs/TEST_REPORT.md` |
| O que o player não faz, e por quê | `docs/LIMITACOES.md` |
| Divergências do visual de referência | `docs/APROXIMACOES.md` |
| Defeitos encontrados em uso | `docs/DEFEITOS.md` |

## Arquitetura em uma linha

Três camadas: `src/core/` (sem Qt, sem API de sistema, sem `#ifdef` — conferido a cada build pelo alvo `core_purity`, AR-07), `src/platform/` (áudio e integração com o sistema) e `src/ui/` (Qt).

## Compilar e testar

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

A suíte roda sem tela e sem placa de som. Testes que dependem de PulseAudio, D-Bus ou do Linux se declaram **pulados** (código 77) onde o recurso falta.

O CI (`.github/workflows/ci.yml`) constrói e testa nos três sistemas, e monta AppImage, Flatpak e o instalador do Windows. Tag `v*` publica um release como **rascunho**.

## Regras de trabalho

Aprendidas neste projeto, quase todas depois de um defeito.

**Verificar medindo, nunca olhando.** Alinhamento visual se confere com `tests/test_layout.cpp`, que imprime a grade e a verifica; ajuste a olho não converge. Um defeito se diagnostica pelo número — um código de saída, uma posição em pixels — e não pela teoria mais plausível.

**Teste de mutação em toda lógica nova.** Quebrar o código de propósito e confirmar que o teste reprova. Um teste que continua verde sem a regra que diz testar é vácuo, e isso já aconteceu aqui mais de uma vez.

**Duas rotas para a mesma operação sempre divergem.** Aconteceu oito vezes: probe e decoder, desenho e clique da barra de rolagem, restauração de sessão e inclusão de arquivos, gravação e leitura de configuração, flags da janela no app e no teste, as duas rotas que declaram o dispositivo de áudio perdido, o título "do que está tocando" decidido em dois lugares, e o leitor de tags abrindo fontes sem as opções de rede — o que deixava passar protocolo proibido. A correção é unificar numa rota só, não remendar a que quebrou.

**Esperar o evento, nunca amostrar num instante arbitrário.** Testes que dormem um tempo fixo e depois conferem o estado são intermitentes. E o `sleep` do Windows tem resolução de ~15 ms: um teste que consome áudio com esperas curtas fica mais lento que o tempo real lá, e só lá.

**Um teste que passa só no Linux não prova nada.** Verificar que a implementação ANTIGA reprova no próprio teste novo: já houve teste que aprovava o código com defeito aqui e só reprovava no macOS, por acaso de tempo.

**Não desfazer decisão visual já aceita sem consultar.** Mudança de aparência se propõe antes de aplicar.

**Construído não é verificado.** Um pacote que o CI monta ainda precisa ser executado numa máquina real antes de o requisito virar OK. O mesmo vale para integração com o sistema (painel de mídia, teclas).

**Nada pessoal ou da máquina no repositório.** O repositório é público: nenhum caminho de diretório pessoal, nome de usuário, e-mail ou dado de configuração local em código, documentação ou mensagem de commit.

**O log fica em `playampng.log`, ao lado da configuração**, nos três sistemas, com a execução anterior em `.1`. Toda linha tem hora. É o primeiro lugar a olhar num relato de defeito.

**Teste de mutação restaura o arquivo com `trap`.** Uma sessão interrompida no meio de uma mutação já deixou um arquivo mutado no disco; sem a restauração garantida, o próximo commit levaria o defeito plantado de propósito.

**Testes precisam devolver `pang::check::exit_code()`.** `PANG_CHECK` registra a falha e segue; o `main` que devolve 0 descarta o registro.

## Estado — setembro de 2026

167 requisitos OK, 22 testes, CI verde em Linux, Windows e macOS.

| Marco | Situação |
|---|---|
| M0–M7 | concluídos |
| M8 — Windows | concluído: instalador NSIS, integração SMTC, verificados em máquina real |
| M9 — macOS | não iniciado. O CI compila e testa no macOS, mas não há pacote nem integração com o painel de mídia, e ninguém no projeto tem um Mac para verificar |

**Pendentes**

| Requisito | O que falta |
|---|---|
| EN-10 | Pacote macOS (M9) |

**Decisões de plataforma que parecem defeito e não são**

- No Wayland o modo destacado fica desabilitado, com a razão escrita no menu (AP-18): o compositor não deixa o cliente posicionar a própria janela, então não há encaixe. Sob `QT_QPA_PLATFORM=xcb` funciona.
- O AppImage exige glibc 2.39 (Ubuntu 24.04+), porque é montado nessa base. O Flatpak não tem esse piso.
- O movimento em grupo das janelas destacadas é assimétrico, como no Winamp: o painel principal arrasta o conjunto, uma janela secundária se desprende.
