# EscapeRoom-HALLtoRelay — Versão simplificada

O relé pode estar no próprio Mestre (instalação local, de fábrica) ou num Remoto por ESP-NOW (instalação remota, escolhida na webapp). A configuração individual dos sensores continua arquivada em `SPEC_V2_futura.md`.

## Objetivo
Abrir uma fechadura eletromagnética, através de um relé, quando os jogadores deixam as peças na combinação certa sobre até quatro sensores HALL.

## Hardware
Instalação local (de fábrica): tudo ligado ao mesmo AtomS3 Lite, sem Remoto nem ESP-NOW. Instalação remota: ver "Relé remoto".

| Componente | Função |
|---|---|
| AtomS3 Lite | Lê os sensores, avalia a combinação, aciona o relé e disponibiliza a webapp |
| Atomic Port ABC Base | Portas A, B e C para os sensores 1 a 3 |
| 4 × HALL Unit | Sensores |
| 1 × Relay Unit | Aciona a fechadura |
| Unit Hub | Só com 4 sensores em modo local: liga ao Grove do AtomS3 Lite, partilhado pelo sensor 4 e pelo relé. Em local com 1 a 3 sensores o relé liga direto ao Grove; em remoto não há Hub |

### Ligações

| Dispositivo | Ligação | GPIO |
|---|---|---|
| Sensor 1 | Porta A da Port ABC | 39 |
| Sensor 2 | Porta B da Port ABC | 8 |
| Sensor 3 | Porta C da Port ABC | 6 |
| Sensor 4 | Grove do AtomS3 Lite (pelo Hub em modo local; direto em modo remoto), fio branco | 1 |
| Relé (DIN) | Grove do AtomS3 Lite (direto com 1 a 3 sensores, pelo Hub com 4; em modo remoto, no Grove do Remoto), fio amarelo | 2 |

## Funcionamento
- Sensor **ativo** = íman detetado (pino LOW). A inversão é uma só, para todos os sensores em uso.
- Com N sensores em uso, sem inversão: a combinação de abertura é os sensores 1 a N todos **sem íman** (peças retiradas) em simultâneo, e a combinação de rearme é todos **com íman** (peças colocadas). Com inversão, as duas trocam-se.
- O jogo começa com as peças sobre os sensores.

1. **Armado:** o relé está em repouso e o sistema lê os sensores. Arranca sempre neste estado.
2. **Abertura:** se a combinação de abertura se mantiver durante o tempo de estabilidade, o relé é ativado e mantém-se ativado até ao rearme. Se a combinação mudar antes, a contagem é cancelada.
3. **A aguardar rearme:** o sistema não volta a abrir, mesmo que a combinação de abertura se mantenha.
4. **Rearme:** quando a combinação de rearme se mantém durante o tempo de estabilidade, o relé é desativado e o sistema volta a Armado.

O relé é ativado com nível HIGH no pino de controlo (NO e COM ligados) e está em repouso com LOW, segundo o exemplo oficial da M5Stack. A lógica do relé não é configurável.

## Relé remoto (ESP-NOW)
Na instalação remota o relé (Relay Unit) está ligado a um segundo AtomS3 Lite, o Remoto (`Remote/Remote.ino`), e o Mestre mantém sensores, jogo e webapp. A lógica do jogo não muda; só muda onde o relé é acionado (`setRelay()`).
- O Mestre envia a ordem (relé ativo ou repouso) com número de sequência; o Remoto responde com ACK. Sem ACK, reenvio sem limite (250 ms a 2 s); erro assinalado à 5.ª falha. Um rearme cancela uma ordem pendente.
- O Remoto arranca sempre com o relé em repouso.
- Canal Wi-Fi 1 fixo, comum ao AP e ao ESP-NOW. MACs fixos no firmware (sem emparelhamento): se uma placa for substituída, atualizar os MACs e recarregar as duas.
- Heartbeat de 1 s; o Remoto é dado como offline sem pacotes durante 3 s.
- Perda do Mestre: com o relé ativo e 60 s sem pacotes do Mestre, o Remoto volta a repouso. Quando o Mestre volta (arranca Armado), a ordem caducada não reativa o relé; o erro fica registado e visível na webapp. Só uma nova abertura, depois de um rearme, ativa o relé.
- Perda do Remoto (Mestre ligado): com a ordem ativa e 60 s sem pacotes do Remoto, o Mestre dá a ordem por caducada e regista o erro. O jogo fica em "A aguardar rearme" (não reabre), e quando o Remoto volta recebe o repouso.
- LED do Mestre (só em modo remoto): pulso vermelho de 150 ms a cada 2 s enquanto a ligação ao Remoto estiver em baixo.
- Em modo local o ESP-NOW e o heartbeat ficam inativos.

## Parâmetros ajustáveis

| Parâmetro | Valor de fábrica |
|---|---|
| Número de sensores em uso (1 a 4) | 4 |
| Inversão da lógica dos sensores | Sem inversão |
| Tempo de estabilidade | 3 s |

**Opção de instalação (Ajustes, cartão "Instalação"):** relé **local** (de fábrica) ou **remoto**, guardada na NVS (chave `rm`) e lida no arranque. A mudança só é aceite com o jogo **Armado**, grava a opção e **reinicia o Mestre** (o AP desliga-se). A reposição dos valores de fábrica **não** altera esta opção.

Nome do dispositivo e do AP: `RSwitchE-Lock-XXXX`, onde XXXX são os dois últimos bytes do MAC do SoftAP em hexadecimal maiúsculo (por exemplo `RSwitchE-Lock-A1B2`), calculado no arranque, sem configuração; o nome completo é mostrado no cabeçalho e no título da webapp. Palavra-passe do AP: `mission2escape` (fixa e igual em todos os dispositivos). Os valores mantêm-se depois de desligar o AP ou a alimentação.

## Modos de operação
- **Normal:** o AP e a webapp ficam desligados, e o jogo funciona de forma autónoma. É o modo em que arranca sempre.
- **Configuração:** um toque longo (3 s) no botão liga o AP, e outro toque longo desliga-o, e o estado é indicado no LED. Não há temporizador de desligar.
- O jogo continua a funcionar normalmente enquanto o AP está ligado.

## Feedback visual
- O LED indica o modo de operação (normal ou configuração).
- Os restantes estados a indicar no LED são propostos na implementação e aprovados antes de serem implementados.

## Webapp
- **Ajustes:** número de sensores, inversão e tempo de estabilidade. Repor os valores de fábrica, com pedido de confirmação.
- **Calibração:** estado de cada sensor em uso, em direto.
- **Ajustes — Instalação:** seletor do relé (local/remoto), com confirmação, aplicado com reinício e só com o jogo Armado.
- **Debugging:** em modo remoto, cartão "Relé remoto" (ligação, relé no Remoto, ordem, falhas, último erro, arranque do Remoto). Botão **Abrir**, que ativa o relé como numa abertura normal e passa a "A aguardar rearme"; o rearme normal desativa-o (se a condição de rearme já se verificar, o relé desativa ao fim do tempo de estabilidade). Visualização do estado do jogo.

## Atualização de firmware
Por USB, no Mestre e (na instalação remota) no Remoto.
