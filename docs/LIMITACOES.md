# PlayAmpNG — Limitações conhecidas

EN-08. O que o player **não** faz, e por quê. Cada item traz a razão, e não só o fato.

## Pendentes para distribuição

| Item | Situação |
|---|---|
| Flatpak não construído | O manifesto está escrito e o YAML é válido, mas `flatpak-builder` não existe na máquina de referência. Nunca foi executado — e um pacote que nunca foi montado não é um pacote |
| `url-homepage-missing` no AppStream | O projeto não tem repositório público. O validador confere se o endereço responde, então um link inventado reprovaria igual e ainda mentiria. **Obrigatório antes de submeter ao Flathub** |
| Windows e macOS | `integration_none.cpp` no lugar de SMTC e MPNowPlayingInfoCenter. O player roda, só não aparece no painel do sistema nem recebe teclas de mídia pelo caminho do ambiente. M8 e M9 |

## Funcionalidades fora de escopo

| Item | Por quê |
|---|---|
| Reordenar a playlist arrastando (`LI-04`) | Regressão consciente do M5, ao trocar o painel para desenho em sprites. Registrada em `DEFEITOS.md` como A-12 |
| Encaixe magnético entre painéis destacados | O protocolo Wayland não informa a posição absoluta da janela ao cliente, então não há como calcular o encaixe. Documentado em `ARCHITECTURE.md §8` |
| Presets do usuário no equalizador (`EQ-07`) | Criar e salvar funciona; editar e excluir pela interface ficou pendente |
| Cálculo de ReplayGain | O player **lê** as tags, não calcula. Calcular seria um scanner de biblioteca, outro programa |
| Velocidade de reprodução variável | Não previsto na especificação, e mexeria no contrato de gapless |

## Saída com escala fracionária torna o desenho impreciso

O player desenha pixel a pixel e escala por múltiplos inteiros, com vizinho mais próximo. Isso pressupõe que um pixel de arte vire um número **inteiro** de pixels de tela.

Quando a saída do sistema usa escala fracionária, essa premissa cai. Medido na máquina de desenvolvimento, com monitores em escala 1,6:

| Escala de arte | Pixels de tela por pixel de arte | Efeito |
|---|---|---|
| 1× | 1,60 | barras de 3 px alternam entre 4 e 5 |
| 2× | 3,20 | barras alternam entre 9 e 10; passo entre 12 e 13 |
| 3× | 4,80 | idem |
| 4× | 6,40 | idem |

A relação é `pixels_de_tela = escala_da_arte × escala_da_saída`. Com saída em 1,6 isso só dá inteiro se a escala de arte for múltiplo de 5 — e 5× significaria uma janela de 1375 px lógicos, inviável.

**O reamostramento acontece depois do buffer do aplicativo**, no compositor, então não há correção possível do lado do código. Verificação direta: uma janela de 550 × 232 lógicos produz um framebuffer de 880 × 371, fator 1,600 exato.

**Como obter o desenho exato:** usar escala inteira de monitor (1 ou 2). No Hyprland:

```
monitor = eDP-2, preferred, auto, 2
```

Com escala de saída inteira, qualquer escala de arte do player cai em pixels inteiros e o desenho fica exato.

O aplicativo detecta a situação na partida e registra um aviso, em vez de deixar o usuário concluir que o player simplesmente é borrado.
