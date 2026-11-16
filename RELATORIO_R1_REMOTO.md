# Relatório R1 — relé remoto por ESP-NOW

## Estado
Código da R1 gerado, compilado e testado em hardware (Mestre `/dev/cu.usbmodem1101`, Remoto `/dev/cu.usbmodem101`).
Flash: Mestre remoto 944 325 B (28 %), Mestre local (`RELAY_REMOTE 0`, só compilado) 934 221 B (27 %), Remoto 879 281 B (26 %).

## Ficheiros
- `Webapp/Protocol.h` = `Remote/Protocol.h` (verificado com `diff`)
- `Webapp/RemoteLink.h`, `Remote/Remote.ino`
- `Webapp/Webapp.ino` alterado (`RELAY_REMOTE 1` forçado; `DEBUG_SERIAL` temporário)

## Resultados dos testes
| # | Teste | Resultado |
|---|---|---|
| 1 | Remoto sozinho: repouso, MAC no Serial | OK (LED confirmado pelo utilizador) |
| 2 | Heartbeat, offline ao desligar, regresso | OK (3 s exatos não medidos) |
| 3 | Abertura e rearme com sensores reais | OK, ACK em 3–7 ms (estabilidade de 3 s não medida no Serial) |
| 4 | Remoto desligado na abertura | OK, erro à 5.ª falha em 5,75 s, relé ativado ao voltar |
| 5 | ACK perdido (`TEST_DROP_ACKS`) | OK, ordem aplicada uma só vez, depois só "same seq" |
| 6 | Rearme com ordem pendente | OK, Remoto nunca ativou o relé |
| 7 | Remoto reinicia com relé ativo | OK, heartbeat reativa em ~2,4 s |
| 8 | Mestre reinicia | OK, masterBoot novo aceite, relé solto |
| 9 | Mestre desligado com relé ativo | OK com 10 s; 60 s reais NÃO testados |
| 10 | Remoto fora além da caducidade | OK com 10 s; 60 s reais NÃO testados |
| 11 | AP ligado e desligado durante o jogo | OK, sem LINK OFFLINE; abertura após "AP off" não exercitada |
| 12 | MAC alheio ignorado | NÃO feito (precisa de terceira placa) |

## Pontos em aberto
- Anomalia única, não reproduzida: uma vez, após o Remoto voltar de uma abertura pendente, o Mestre não registou nenhum ACK durante >40 s (o Remoto recebia os CMD). Um reinício do Mestre resolveu. Fica a vigiar em R3.
- As placas ficaram carregadas com o build de teste de 10 s (`TEST_SHORT_EXPIRY`). No código-fonte a flag está desligada (60 s). Antes da R3 é preciso recarregar as duas placas.
- Flags temporárias a retirar em R3: `DEBUG_SERIAL` (Mestre e Remoto), `TEST_SHORT_EXPIRY`, `TEST_DROP_ACKS`.
- O aviso "peer had to be re-added" no arranque do Mestre é só o primeiro `add_peer`; rever o texto em R3.
- `arduino-cli upload` não recompila: usar `compile -u` depois de alterar flags.

## Fora de R1 (R2)
Seletor na webapp, chave NVS `rm`, cartão de ligação e endpoints, atualização de `SPEC.md`/`CLAUDE.md`.
