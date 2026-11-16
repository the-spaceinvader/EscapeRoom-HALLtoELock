# Fase 3 — Relatório de validação (versão simplificada)

Base: `Webapp/Webapp.ino` + `Webapp/WebPage.h` (Fase 2), jogo da Fase 1 incluído.
Placa: AtomS3 Lite, FQBN `m5stack:esp32:m5stack_atoms3:CDCOnBoot=cdc`.
Método: o utilizador mexeu nas peças e na webapp; o Serial (com `DEBUG_SERIAL` e, só nos testes, logs adicionais de `Sensors:` e `Count start/cancel`) foi lido pelo Claude. Tempos = diferença de timestamps `millis()`.

## Resultado

| # | Teste | Resultado | Evidência |
|---|---|---|---|
| 1 | Arranque com peças | Passou | LED verde, relé sem clique (observação). O `Sensors:` inicial perdeu-se na reenumeração USB |
| 2 | Retirar todas, manter 3 s | Passou | Count start → Relay ON = **3000 ms** (133388→136388; repetido em 385132→388132, 460193→463193, 295701→298701, 310656→313656); relé mantém-se, LED azul |
| 3 | Repor uma antes dos 3 s | Passou | Count start 411911 → Count cancel 413033 (1122 ms), sem Relay ON |
| 4 | Relé ativo, mexer nas peças | Passou | ~40 s e 30 mudanças de sensor, sem Relay OFF/ARMED; relé físico sem mudança (confirmado pelo utilizador) |
| 5 | Repor todas, manter 3 s | Passou | Count start → Relay OFF = **3000 ms** (276324→279324; repetido várias vezes) |
| 6 | Repor todas e tirar uma antes dos 3 s | Passou | Count start 500891 → cancel 502132 (1241 ms), sem Relay OFF, fica em A aguardar rearme |
| 7 | Cortar a corrente com o relé ativo | Passou | LED verde, relé em repouso (observação); uptime baixo comprovado no teste 8 |
| 8 | Religar com peças retiradas | Passou | Relay ON a 3016 ms de uptime |
| 9 | N = 1, 2, 3, 4 | Passou | Aberturas e rearmes a 3000 ms; sensores acima de N ignorados (N=2 aberto com `0011`, N=3 aberto com `0001`, N=1 aberto com 2 e 3 colocados). Foi preciso repetir passos por ordem trocada (não é falha do firmware) |
| 10 | Inversão | Passou | Abertura com todas colocadas, rearme com todas retiradas, ambos 3000 ms |
| 11 | Mudar a estabilidade | Passou | 1 s: abertura 1000 ms e rearme 1000 ms; 5 s: abertura 5000 ms e rearme 5000 ms |
| 12 | Persistência após corte de corrente | Passou | Após reinício, Relay ON a 2016 ms de uptime com N=3, inversão e 2 s; webapp mostra N=3 e invertido |
| 13 | Repor valores de fábrica | Passou | Uma só linha `Settings: sensors=4 invert=no stability=3000 ms` (confirmar); cancelar não gerou alteração |
| 14 | Toque longo liga/desliga o AP | Passou | `Long press: AP ON/OFF`; toque curto sem efeito; AP ligado ~2 h sem desligar sozinho; LED: 3 piscas e pulsos (1 longo sem cliente, 2 curtos com cliente, confirmado pelo utilizador) |
| 15 | Ciclos completos com AP e telemóvel | Passou | 4 ciclos a 3000 ms, sem eventos espúrios. O comportamento da webapp durante os ciclos não foi descrito pelo utilizador |
| 16 | Alterar parâmetros a meio da contagem e com o relé ativo | Passou | Cada `Settings` reinicia a contagem, mantém estado e relé; Relay OFF 4000 ms e Relay ON 3000 ms depois do último `Settings`; sem estado incoerente |
| 17 | Botão Abrir | Passou | Com `1111`: Relay ON e Relay OFF 3000 ms depois. Com peças em abertura: Relay ON imediato e mantém-se ≥8 s; rearme 3000 ms |
| 18 | Calibração em direto | Dispensado pelo utilizador | Evidência parcial: confirmação verbal do ecrã e mudanças dos 4 sensores no log |

## Pontos pedidos

- **Tempos (2, 5, 6, 11):** 2 e 5 = 3000 ms exatos em todas as repetições; 6 cancelado após 1241 ms sem rearmar; 11 = 1000 e 5000 ms exatos. Resolução = 1 ms.
- **Duas desativações inesperadas da Fase 1:** **não se repetiram.** Maior prova: após o teste 8 o relé ficou ativo de 3016 ms até 7759249 ms (~2 h 9 min, incluindo o AP ligado ~2 h) sem nenhum `Relay OFF` não provocado por um rearme. Também não ocorreram nos testes 4, 14, 15, 16 e 17. Não se pode garantir a ausência total, mas não há sinal de que existam nesta versão.
- **Cenário real (fechadura verdadeira, tempo de retirada das peças):** **dispensado pelo utilizador, não executado.**
- **Retirar o `DEBUG_SERIAL`:** feito com autorização (removidos o `#define`, todos os blocos `#if DEBUG_SERIAL`, o `Serial.begin` e `SERIAL_BAUD`) e carregado na placa. **Repetição dos testes 2 e 5 sem `DEBUG_SERIAL`: dispensada pelo utilizador, não executada.** A versão sem Serial só foi compilada e carregada, sem teste funcional.

## Falhas por corrigir
Nenhuma falha de firmware encontrada.

Observações menores, sem impacto verificado:
- Em 3 de cerca de 10 aplicações de ajustes houve dois `Settings:` seguidos (a ~1,5 s; ex.: 7915772/7917376, 8323754/8325201, 458719/459302 ms). Pode ser duplo toque no botão da webapp ou duplo envio. A segunda aplicação só reinicia a contagem. Por confirmar.
- Ao usar o Serial, as linhas de arranque perdem-se na reenumeração USB (limitação do método, não do firmware).

## Alteração posterior: nome do AP com sufixo do MAC
Decisão do utilizador (várias unidades na mesma sala): o nome do AP e do dispositivo passou de `RSwitchE-Lock` (fixo) para `RSwitchE-Lock-XXXX`, com XXXX = últimos 2 bytes do MAC do SoftAP em hexadecimal maiúsculo (lidos com `esp_read_mac(…, ESP_MAC_WIFI_SOFTAP)` no arranque). Sem configuração nem NVS; palavra-passe `mission2escape` inalterada.
- `Webapp/Webapp.ino`: `apSsid` + `buildApSsid()`, usado em `softAPsetHostname` e `softAP`; `/api/status` devolve `ap.ssid` (`body` 260 → 300 bytes).
- `Webapp/WebPage.h`: `<h1>` e título do separador atualizados com `ap.ssid` em `renderStatus`.
- `SPEC.md` (parâmetros ajustáveis) atualizado.
- Compilado sem erros (27 % da flash) e carregado em `/dev/cu.usbmodem1101` com autorização. Teste em hardware pelo utilizador: **"tudo a funcionar na perfeição"** (nome, webapp, jogo e relé). Não foram registadas evidências de Serial nem testado um segundo dispositivo em separado neste relatório.
- A opção C (nome editável na webapp) ficou adiada.

## Estado
Código final: `Webapp/Webapp.ino` sem `DEBUG_SERIAL`, com o nome do AP com sufixo do MAC, carregado em `/dev/cu.usbmodem1101`. Backup da versão com debug fora do repositório (scratchpad da sessão).
