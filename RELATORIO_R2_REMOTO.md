# Relatório R2 — opção de instalação (relé local / remoto)

## Estado
R2 gerada, compilada e testada em hardware pelo utilizador. Mestre `/dev/cu.usbmodem1101` (932 816 B, 71 %), Remoto `/dev/cu.usbmodem101` (868 336 B, caducidade de 60 s). Ambas sem `TEST_SHORT_EXPIRY` nem `TEST_DROP_ACKS`; `DEBUG_SERIAL` ainda ativo.

## Alterações
- `Webapp/Webapp.ino`: `RELAY_REMOTE` retirado; `relayRemote` lido da NVS (chave `rm`, ausente = local) no arranque; `POST /api/relaymode` (só com o jogo Armado, grava e reinicia ~800 ms depois); `/api/status` com `rm` e `st`; `/api/live` com bloco `link`. A reposição de fábrica não toca no modo.
- `Webapp/RemoteLink.h`: sem `#if`; só é chamado em modo remoto (em local não há ESP-NOW nem heartbeat).
- `Webapp/WebPage.h`: cartão "Instalação" (Ajustes) e cartão "Relé remoto" (Debugging, só em modo remoto).
- `SPEC.md` e `CLAUDE.md` atualizados. `Protocol.h` e `Remote/` sem alterações.

## Resultados
| # | Teste | Resultado |
|---|---|---|
| L1 | Arranque em local, sem ESP-NOW | OK (Serial) |
| L2 | Abertura e rearme com relé local | OK |
| L3 | Botão "Abrir" | OK |
| L4 | Toque longo, LED, sem pulso vermelho | OK |
| L5 | `rm:0`, cartão Instalação, sem cartão remoto | OK |
| L6 | Repor valores de fábrica | OK |
| M1 | Passar a remoto com o jogo Armado | OK |
| M2 | Persistência após corte de alimentação | OK (Serial: REMOTE, ACK em 4 ms, LINK ONLINE) |
| M3 | Recusa fora de Armado | OK (botão desativado e mensagem) |
| M4 | Cartão "Relé remoto" e rearme | OK |
| M5 | Voltar a local, relé no Mestre | OK (Serial: LOCAL, sem ESP-NOW; Remoto com pulso vermelho, esperado) |
| M6 | Escolher o modo atual | OK ("Sem alterações.", sem reinício) |

## Não testado / em aberto
- A recusa 409 do servidor (só se testou o botão desativado na página).
- Caducidade de 60 s reais em modo remoto (fica para a R3).
- Anomalia da R1 (Mestre sem registar ACK >40 s): não ocorreu nos testes da R2; continua por esclarecer.
- Se alguém abrir o jogo nos ~800 ms entre o pedido de mudança de modo e o reinício, o reinício acontece na mesma (jogo volta a Armado, relé em repouso).
- Aviso "peer had to be re-added" no arranque do Mestre: é o primeiro `add_peer`; rever o texto na R3.

## R3
Retirar `DEBUG_SERIAL`, `TEST_SHORT_EXPIRY`, `TEST_DROP_ACKS`; validação final (inclui 60 s reais e teste do MAC alheio).
