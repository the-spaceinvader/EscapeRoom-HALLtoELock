# Relatório R3 — limpeza e validação final do relé remoto

## Estado
Flags e logs de teste retirados do código-fonte; build final carregado nas duas placas (Mestre `/dev/cu.usbmodem1101`, 930 797 B; Remoto `/dev/cu.usbmodem101`, 867 009 B) e teste funcional curto OK em modo remoto e em modo local (LED e relé, observados pelo utilizador, sem Serial).

## Parte 1: limpeza
- Retirados: `DEBUG_SERIAL` e a macro `DBGT` (Mestre e Remoto), `Serial.begin`, `TEST_DROP_ACKS`, `TEST_SHORT_EXPIRY` (`EXPIRY_MS = 60000` fixo) e os logs temporários `TEMP-R3`. Os dois `Protocol.h` continuam idênticos (`diff` limpo).
- O aviso "peer had to be re-added" desapareceu com o `DEBUG_SERIAL` (era só o primeiro `add_peer`).
- Sem alterações funcionais. Compilação sem avisos novos (só o `neopixelWrite` obsoleto, já existente).
- Método: medições com `DEBUG_SERIAL` ativo e, só depois, build final. Hora do Mac em cada linha do Serial.

## Parte 2: resultados medidos (Serial)
| # | Teste | Resultado |
|---|---|---|
| T1 | Estabilidade (local) | 3000 ms exatos em 3 contagens (abertura, rearme, abertura); contagens canceladas aos 2543 ms e 2151 ms sem abrir nem rearmar |
| T2 | Deteção offline | último `CMD same` 17:04:18.447, `LINK OFFLINE` 17:04:21.450: 3003 ms |
| T3 | Recusa 409 | `curl -X POST -d rm=0 .../api/relaymode` com o jogo em "A aguardar rearme": 409 "Só é possível mudar com o jogo Armado.", sem reinício |
| T4 | 60 s reais, Mestre desligado com relé ativo | Remoto: `EXPIRED: no Master for 60000 ms`, relé em repouso (último CMD 17:07:47.915, EXPIRED 17:08:47.915) |
| T5 | Remoto fora além da caducidade (Mestre ligado) | Mestre: `LINK OFFLINE` às 17:09:58, ordem caducada aos 60 s (17:10:55, erro kind=3), ordem alvo de repouso; ao voltar, o Remoto recebeu o repouso e o relé não reativou |
| T6 | AP ligado durante o jogo, modo remoto; abertura depois de "AP off" | OK: rearme, `AP off` e nova abertura com ACK em 4–5 ms, sem `LINK OFFLINE` |
| T7 | MAC alheio (3.ª placa, `AC:A7:04:00:45:74`) | CMD válido com `target=1` enviado a cada 2 s durante pelo menos 25 s e entregue ao nível do rádio; o Remoto não registou nenhum `CMD new` e o relé não ativou (confirmado pelo utilizador no LED) |
| T8 | Anomalia da R1 (ACK em falta após regresso do Remoto) | Não reproduzida: 3 recuperações de ordens pendentes (2 de abertura, 1 de repouso), ACK 2–4 ms depois do CMD; Remoto a receber o CMD 0,15–1,6 s após o arranque |
| F | Teste funcional do build final sem Serial | Remoto: repouso, abertura e rearme OK. Local: abertura e rearme OK |

## Não testado / limitações
- **Serial do build final:** não existe; o teste funcional foi por LED e relé.
- **T5:** o conteúdo do cartão "Relé remoto" da webapp (erro visível) não foi registado nesta sessão. Os erros `kind=3` (`LINK ERROR`) vê-se no Serial do Mestre.
- **T7:** só o lado do Remoto foi testado (não o Mestre a receber ACK alheio). A 3.ª placa tinha outro firmware, que foi apagado com autorização.
- **T8:** o ciclo 1 não ficou medido (a leitura do Serial começou depois da abertura); contam os três seguintes. Dois erros de ordem dos passos (um rearme antes do regresso, outra sequência incompleta) foram descartados.
- **Janela de ~800 ms** entre o pedido de mudança de modo e o reinício: não testada.
- Não se testou o Mestre com o AP ligado por períodos longos (horas).

## Riscos residuais
- **Anomalia da R1** (Mestre sem registar ACK durante mais de 40 s depois de o Remoto voltar de uma abertura pendente): vista uma vez e não reproduzida. A causa continua desconhecida. Se voltar a ocorrer, um reinício do Mestre resolve.
- **Canal Wi-Fi 1 fixo:** a rever no local face às redes Wi-Fi existentes.
- **Janela de ~800 ms** na mudança de modo: se alguém abrir o jogo nesse intervalo, o reinício acontece na mesma (jogo volta a Armado, relé em repouso).
- **Substituição de placa:** MACs fixos no firmware; uma avaria obriga a recarregar o par completo.
- **Discrepância documental resolvida só em texto:** a SPEC dizia que o Mestre volta a Armado ao perder o Remoto. O comportamento do código (e testado) é ficar em "A aguardar rearme"; a SPEC foi corrigida e passou a distinguir "perda do Mestre" de "perda do Remoto". Confirmado pelo utilizador em 2026-11-16: aceita "A aguardar rearme" ao perder o Remoto.

## Ficheiros alterados
`Webapp/Webapp.ino`, `Webapp/RemoteLink.h`, `Webapp/Protocol.h`, `Remote/Remote.ino`, `Remote/Protocol.h`, `CLAUDE.md`, `SPEC.md`.
