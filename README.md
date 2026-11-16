# RSwitchE-Lock — fechadura de escape room por sensores HALL

Abre uma fechadura eletromagnética, através de um relé, quando os jogadores retiram as peças de até quatro sensores HALL na combinação certa. O relé fica ativo até as peças voltarem a ser colocadas (rearme). Usa um AtomS3 Lite (ESP32-S3) com uma página de configuração no telemóvel.

O relé pode estar:
- **no próprio Mestre** (instalação local, de fábrica), ou
- **num segundo AtomS3 Lite, o Remoto**, que recebe as ordens por ESP-NOW (instalação remota). A escolha faz-se na webapp.

## Documentação

| Ficheiro | Conteúdo |
|---|---|
| [`SPEC.md`](SPEC.md) | Especificação funcional (a referência) |
| [`Manual_Instalacao.md`](Manual_Instalacao.md) | Manual de instalação: ligações, firmware, substituição de placa e primeira colocação em funcionamento |
| [`Manual_Utilizacao.md`](Manual_Utilizacao.md) | Manual de utilização |
| [`RELATORIO_FASE3_VALIDACAO.md`](RELATORIO_FASE3_VALIDACAO.md) | Validação do jogo e da webapp |
| [`RELATORIO_R1_REMOTO.md`](RELATORIO_R1_REMOTO.md), [`R2`](RELATORIO_R2_REMOTO.md), [`R3`](RELATORIO_R3_REMOTO.md) | Relé remoto: protocolo, opção na webapp e validação final |

## Hardware

| Instalação | Componentes |
|---|---|
| Local | [AtomS3 Lite](https://docs.m5stack.com/en/core/AtomS3-Lite), [Atomic Port ABC Base](https://docs.m5stack.com/en/atom/Atomic_Port_ABC_Base), 4 × [HALL Unit](https://docs.m5stack.com/en/unit/Unit_Hall), [Relay Unit](https://docs.m5stack.com/en/unit/Unit_Relay), [Unit Hub](https://docs.m5stack.com/en/unit/Unit_Hub) |
| Remota | O Mestre sem a Relay Unit, mais um segundo AtomS3 Lite com a Relay Unit |

Sensores nas portas A, B e C da Port ABC (GPIO 39, 8 e 6) e na Grove (GPIO 1). Relé (DIN) na Grove, no GPIO 2.

## Firmware

| Pasta | Nó |
|---|---|
| `Webapp/` | Mestre (`Webapp.ino`, `WebPage.h`, `RemoteLink.h`, `Protocol.h`) |
| `Remote/` | Remoto (`Remote.ino`, `Protocol.h`) |

Os dois `Protocol.h` têm de ser **idênticos**:

```bash
diff Webapp/Protocol.h Remote/Protocol.h
```

Não são precisas bibliotecas externas. Core `esp32:esp32` (testado com a versão 3.3.11):

```bash
arduino-cli compile --fqbn esp32:esp32:m5stack_atoms3:CDCOnBoot=cdc Webapp
arduino-cli compile --fqbn esp32:esp32:m5stack_atoms3:CDCOnBoot=cdc Remote
```

### Antes de usar com as suas placas

**Troque os valores do repositório pelos seus:**

- **Endereços MAC** (`MASTER_MAC` e `REMOTE_MAC`, nas **duas** cópias de `Protocol.h`): são os das placas originais. Só contam na instalação remota.
- **Palavra-passe do ponto de acesso** (`AP_PASSWORD`, em `Webapp/Webapp.ino`): o valor do repositório é público.

Detalhes e o procedimento de substituição de placa estão no [manual de instalação](Manual_Instalacao.md).

## Estado

Jogo, webapp e relé remoto implementados e validados em hardware (ver os relatórios). Riscos residuais e pontos não testados estão no `RELATORIO_R3_REMOTO.md`.

## Licença

GPL-3.0. Ver o ficheiro `LICENSE`.
