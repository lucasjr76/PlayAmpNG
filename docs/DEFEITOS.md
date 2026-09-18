# PlayAmpNG — Defeitos e observações de teste manual

Lista viva. Cada item diz quem observou, o que foi medido e onde foi corrigido. Itens abertos ficam no topo.

---

## Abertos

| # | Observação | Origem | Destino |
|---|---|---|---|
| A-1 | Os três painéis não abrem nem fecham juntos | Teste manual do usuário | M5 parte 2 — modo integrado |
| A-2 | Os painéis têm larguras diferentes entre si | Teste manual do usuário | M5 parte 2 — todos em 275 px |
| A-3 | **Fechar o painel principal deixa os outros abertos e o aplicativo inacessível** | Teste manual do usuário | M5 parte 2 — bloqueador |
| A-4 | Equalizador e playlist ainda usam widgets Qt, destoando do painel principal | Teste manual do usuário ("feia") | M5 parte 2 — sprites |
| A-5 | Em compositor *tiling*, os painéis destacados aparecem empilhados e redimensionados pelo gerenciador | Observado nas capturas | Modo integrado resolve; destacado permanece melhor esforço |

O item **A-3** é o mais grave: fechar a janela primária sem encerrar o processo deixa o usuário sem nenhuma forma de voltar. Vira bloqueador do M5 parte 2.

---

## Corrigidos

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
