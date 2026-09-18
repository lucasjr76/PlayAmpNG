# PlayAmpNG — Defeitos e observações de teste manual

Lista viva. Cada item diz quem observou, o que foi medido e onde foi corrigido. Itens abertos ficam no topo.

---

## Abertos

| # | Observação | Origem | Situação |
|---|---|---|---|
| A-10 | A aparência ainda é tosca — proporções, espaçamento e acabamento | Teste manual do usuário | Passe estético pendente; a estrutura já está no lugar |
| A-11 | Encaixe magnético entre painéis destacados | — | Indisponível no Wayland por restrição de protocolo (`ARCHITECTURE.md §8`) |
| A-12 | `LI-04` — reordenar a playlist arrastando | Regressão consciente do M5 | A reimplementar no painel em sprites |

---

## Corrigidos

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
