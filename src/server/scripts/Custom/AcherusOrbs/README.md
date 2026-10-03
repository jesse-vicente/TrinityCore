# Battle for Acherus

Battleground customizado no estilo **Temple of Kotmogu**, jogado em cópias "phaseadas" do salão central de
**Acherus: The Ebon Hold** (mapa 609). Roda na branch `3.3.5` (cliente 3.3.5a, build 12340) **sem patch de cliente**:
todo o comportamento é server-side e reaproveita só dados que o cliente já conhece.

- Branch: `feature/acherus-orbs` (criada a partir de `3.3.5-local`)
- Worktree: `.worktrees/3.3.5`
- Build dir: `C:\TrinityBuild335` (Visual Studio 17 2022, `RelWithDebInfo`)

## Regras

| Regra | Valor | Origem |
|---|---|---|
| Times | 10v10, Aliança x Horda | warcraft.wiki.gg |
| Vitória | primeiro a 1600 pontos | warcraft.wiki.gg |
| Duração | 25 min, depois vence o maior placar (igual = empate) | decisão do projeto |
| Tick | a cada 5 s, por orbe carregado | script do Kotmogu no TrinityCore `master` |
| Pontos por tick | poço central **6**, plataforma das forjas **4**, fora do salão **2** | valores do retail (script do `master`); a wiki cita 5/4/3 |
| Buff do portador | por acúmulo: +10% dano causado, +30% dano recebido, -5% cura recebida; +1 acúmulo a cada 15 s | Wowhead, spell 121164 (Orb of Power) |
| Escala do portador | +20% ao pegar, +10% por acúmulo, até 2x | decisão do projeto (sem fonte) |
| Orbes | 1 por jogador, sem montaria; morte do portador devolve o orbe à forja | warcraft.wiki.gg |
| Kill | +10 pontos para o time a cada kill de jogador inimigo, portador ou não (sem bônus extra por portador; a spell 112910 não existe no 3.3.5) | decisão do projeto |
| Preparação | 2 min, cada time preso ao seu spawn | padrão de BG do core |
| Ressurreição | ondas de 30 s, só para quem deu "Release Spirit" | padrão de BG do core |
| Desconexão | lugar mantido por 300 s | `MAX_OFFLINE_TIME` das BGs |
| Nível | apenas 80 | decisão do projeto |
| Buff | 1 Berserk (spell 23505) no lugar do portal para o andar de cima, de volta 3 min depois de pego | posição: decisão do projeto; tempo: `BUFF_RESPAWN_TIME` das BGs |
| Montaria | não: o salão é ambiente fechado (o cliente bloqueia e o core remove montarias em ambiente fechado) | limitação do jogo |
| Velocidade | todos recebem 51721 (Dominion Over Acherus, +75% de corrida) na área 4342, como os DKs de Acherus, para compensar a falta de montaria; removida de todos (DKs inclusive) ao sair da partida | decisão do projeto; spell e área do `spell_area` (quest 12657) |

O -5% de absorção recebida do Orb of Power não foi implementado: o 3.3.5 não tem hook para absorções.

## Layout (mapa 609, andar superior)

| Ponto | Posição |
|---|---|
| Centro (poço) | 2459.4, -5593.4, 414.1 (circuncentro das 3 forjas) |
| Forja A, **Frost** | 2493.37, -5642.43, 420.86 |
| Forja B, **Unholy** (centro, oposta à porta) | 2509.31, -5560.39, 420.86 |
| Forja C, **Blood** | 2427.28, -5544.45, 420.86 |
| Porta para a área do Lich King | 2410.68, -5626.74, 420.66 |
| Spawn Aliança / Horda | 2447.56, -5656.40 / 2397.17, -5581.70 (z 420.65) |
| Respawn Aliança / Horda (pontas da plataforma externa) | 2345.44, -5696.87 / 2321.40, -5661.22 (z 426.03) |

Os spawns e respawns são simétricos em relação ao eixo porta → forja B: 47.9 jardas até o orbe lateral, 114.2 até o
Unholy e 97.05 da ponta até a porta, para os dois times.

Zonas de pontuação (`GetPointsForPosition`): poço = raio 25 e z < 418; plataforma = raio 62 e até 56 jardas na direção
da porta; o resto (corredor, área externa, outros andares) = fora.

## Arquitetura

### Arquivos

| Arquivo | Conteúdo |
|---|---|
| `AcherusOrbs.h` | constantes (IDs, posições, timers, pontuação) e as classes `Manager`, `Match`, `MatchPlayer`, `OrbState` |
| `AcherusOrbs.cpp` | toda a lógica: fila, partidas, phases, orbes, mortes, placar, logout/login |
| `AcherusOrbsScripts.cpp` | NPC da fila, forja clicável, `PlayerScript`, `UnitScript`, `WorldScript`, comandos `.acherus` |
| `sql/custom/world/2026_10_01_00_world_acherus_orbs.sql` | NPC 990000, textos de gossip 990000/990001, forjas 990001–990003, buff Berserk 990004 |
| `sql/custom/characters/2026_10_01_00_characters_acherus_orbs.sql` | tabela `custom_acherus_orbs_return` |

Os SQLs são aplicados automaticamente pelo updater do worldserver (`updates_include` já aponta para `sql/custom`).
**Atenção:** `sql/custom/*/.gitignore` ignora `*.sql`, então eles só entram num commit com `git add -f`.

### Hooks adicionados ao core

Todos são `PlayerScript`, sem efeito quando nenhum script os usa.

| Hook | Onde é chamado | Para quê |
|---|---|---|
| `OnSendInitWorldStates` | `Player::SendInitWorldStates`, antes do envio | trocar mapa/zona do pacote pelos do Eye of the Storm e mostrar o placar |
| `OnLeaveBattlefield` | `HandleBattlefieldLeaveOpcode` | botão "Leave Battleground" do placar final |
| `OnCheckSanctuary` | `Player::UpdateArea` | o mapa 609 inteiro é santuário (`AreaTableEntry::IsSanctuary`), o que impede PvP |
| `OnRepopAtGraveyard` | início de `Player::RepopAtGraveyard` | mandar o fantasma para o anjo do time, e não para o cemitério dos DKs |
| `OnSpiritHealerQuery` | clique no Spirit Guide (`NPCHandler`) e `CMSG_AREA_SPIRIT_HEALER_QUERY/QUEUE`, fora de BG/Battlefield | mostrar o tempo até a próxima onda |
| `OnBeforeLogout` | `WorldSession::LogoutPlayer`, junto do `EventPlayerLoggedOut` das BGs, **antes do save** | soltar o orbe antes de o personagem ser salvo |

### Portabilidade

O módulo deve ser levado para outro servidor (wow-brasil, com core próprio), então mudanças no core ficam no mínimo e
nunca alteram o comportamento padrão do jogo. Além dos hooks acima, o módulo depende só de APIs comuns aos cores
3.3.5 derivados do TrinityCore: `Group::SetBattlefieldGroup`, `Player::SetBattlegroundOrBattlefieldRaid`,
`SetClientControl` e os pacotes de BG (`MSG_PVP_LOG_DATA`, `SMSG_BATTLEFIELD_STATUS`, `SMSG_INIT_WORLD_STATES`).

### Raids

Cada time de cada partida tem um raid do tipo "battlefield", sem mudança no core. O `Group` só testa se o ponteiro do
battlefield é nulo e nunca o usa. Por isso o módulo tem uma subclasse vazia de `Battlefield` (`RaidAnchor`), que
nunca é registrada no `BattlefieldMgr` nem atualizada, e a passa para `SetBattlefieldGroup`. Assim o raid funciona
como o de uma BG:
- não é salvo no banco;
- guarda o grupo original de cada membro e o devolve na saída;
- aceita raid de 1 membro;
- mostra o status PvP dos membros.

O raid é criado quando o primeiro jogador do time chega ao mapa. A checagem de 1 s (`CheckPlayers`) religa quem voltou
de um logout, readiciona quem deu /leave ou foi expulso, e tira do raid quem não é do time (por exemplo, alguém
convidado pelo líder). O módulo guarda só o GUID do grupo, porque o `Group` pode se desfazer sozinho dentro do
`RemoveMember`. O chat /bg não funciona, porque exige um `Battleground` de verdade; o /raid funciona.

### Threads

- A lógica da partida roda em `WorldScript::OnUpdate`, chamado depois de todos os mapas terminarem o update
  (mesmo ponto usado por Battlefield/Wintergrasp). Teleportes e spawns entre mapas são seguros ali.
- `_playerMatch` e as listas de partidas só mudam no update do mundo. Os hooks de mapa (dano, cura, clique na forja,
  kill) apenas leem esses dados ou alteram estado da própria partida, que fica toda no mapa 609.
- A fila e as saídas pedidas pelo botão Leave usam `_queueLock`. As saídas são processadas no update do mundo.
- O login roda no thread do mundo. O retorno pós-queda (`_pendingReturns`) também.

### Phasing

- Cada partida usa um bit de phase livre de 9 a 31 (`1u << 9` … `1u << 31`). Acherus já usa os bits 1–256 nas quests de
  DK, então há **até 23 partidas simultâneas**.
- Na phase da partida, os NPCs e GOs originais de Acherus somem, inclusive as forjas. Por isso cada partida spawna os
  seus próprios objetos: clones clicáveis das forjas (type 10, display 8175, size 2.03), bunnies invisíveis
  (`23837`) com o feixe de cada forja e Spirit Guides (`13116`/`13117`).
- A phase de cada participante é reaplicada a cada segundo. Isso cobre auras de phase de quest, que podem ser
  reaplicadas por troca de área.
- Ao sair, a phase é recalculada como em `AuraEffect::HandlePhase`.

### Interface sem patch de cliente

Testado em jogo com os comandos `.debug bgui`, que eram um patch local de `cs_debug.cpp` e não foram commitados:

- **Placar de topo:** o cliente escolhe os frames de world state pelo mapa/zona **informado no pacote**
  `SMSG_INIT_WORLD_STATES`. Mandando mapa 566 / zona 3820, aparece o frame do Eye of the Storm ("Bases: N Victory
  Points: N/1600"). Usamos `2749`/`2750` para os pontos e `2752`/`2753` (Bases) para os orbes que cada time segura.
  Os textos e o teto de 1600 são fixos do cliente.
- **Placar final:** `MSG_PVP_LOG_DATA` com vencedor abre a tela "Alliance/Horde Wins" com KB, mortes, HK, dano e cura.
  Sem vencedor, nada aparece, e `ToggleWorldStateScoreFrame()` não abre fora de uma BG. Não há colunas extras.
- **Timers do placar final:** "Time Elapsed" e "Battleground closing in" vêm de um `SMSG_BATTLEFIELD_STATUS` com status
  active (`StartTimer` e `ShutdownTimer`), como o `Battleground::EndBattleground` envia logo após o placar. O script
  manda esse pacote no fim da partida, num slot de fila de BG que o jogador não está usando, com o QueueID do Eye of the
  Storm e o mapa 566. Na saída, o slot é limpo com status none. O tempo decorrido conta só a partida, sem a preparação.
  O cliente mostra os dois tempos sem segundos, então menos de 1 minuto aparece vazio. Com esse status, o cliente também
  passa a esperar a coluna do EotS (Flag Captures) no placar: cada jogador precisa mandar 1 stat, senão aparece lixo
  de memória. A coluna mostra os pontos que o jogador fez para o time (ticks como portador + bônus de kill). O cliente
  deixa o valor 0 em branco.
- **Contador do Spirit Healer:** o frame nativo não funciona fora de BG. Ao receber `SMSG_AREA_SPIRIT_HEALER_TIME`, o
  cliente abre e fecha o frame em loop e acaba desconectado. Por isso a contagem é feita com `SendAreaTriggerMessage`
  ("Resurrection in N seconds") logo após o Release e aos 30/20/10/5/4/3/2/1 s. As consultas automáticas do cliente
  são consumidas sem resposta.
- **Ícone de BG no minimapa:** aparece por causa do battlefield status do placar final, com o nome "Eye of the Storm".
  O nome vem do `BattlemasterList.dbc` do cliente e não pode ser trocado sem patch.
- **Mensagens de orbe:** `CHAT_MSG_RAID_BOSS_EMOTE`, que o cliente mostra em amarelo no centro da tela e também no
  chat. O nome do orbe vai colorido com códigos `|c` (Frost azul, Blood vermelho, Unholy verde).
- **Pontos:** não há texto de combate nativo para pontos customizados (o "+N Victory Points" do Kotmogu vem de spells
  do retail). Quem pontua recebe "+N points" no topo (`SendAreaTriggerMessage`; o `SendNotification` sai em
  vermelho, como erro): o portador a cada tick e quem deu o golpe final no bônus de kill.
- **Sons** (constantes do core 3.3.5, `PlaySoundToAll` das BGs): 8174 (`BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP`) ao pegar
  um orbe e quando ele volta (morte do portador, logout, saída); 3439 (`SOUND_BG_START`) quando a batalha começa;
  8455/8454 (`SOUND_ALLIANCE_WINS`/`SOUND_HORDE_WINS`) na vitória, nada no empate.

## Fluxo da partida

1. **Fila:** NPC `990000` (posicionar com `.npc add 990000`; hoje fica em Old Town, Stormwind) ou `.acherus queue`.
   Exige nível 80 e o jogador não pode estar em BG ou arena. As filas são separadas por facção.
2. **Início:** quando os dois times atingem o mínimo (`AcherusOrbs.MinPlayersPerTeam`), ou com `.acherus start`, a
   partida pega até `PlayersPerTeam` jogadores elegíveis de cada fila: vivos, fora de combate, fora de voo, fora de
   instância.
   **Vagas em partidas abertas:** antes de criar uma partida, a fila preenche as vagas das partidas em preparação ou em
   andamento (da mais antiga para a mais nova), por facção. Vaga = `PlayersPerTeam` menos os jogadores do time,
   contando os offline dentro dos 300 s. Os times podem ficar desiguais por um tempo, como numa BG. Partida no placar
   final não recebe ninguém. Quem entra com a partida já começada vai direto ao spawn do time, sem preparação.
3. **Entrada:** a posição atual vai para `custom_acherus_orbs_return` e para a memória. O jogador desmonta, recebe a
   phase da partida e é teleportado ao spawn do time. Ao chegar, entra no raid do time (ver Raids); ao sair, volta ao
   grupo que tinha.
4. **Preparação (2 min):** cada time fica preso a 10 jardas do spawn, com avisos aos 60 s e 30 s. Os anjos ficam
   dentro do spawn, como no Warsong Gulch. Quem morre dá Release e ressuscita ali, na onda.
5. **Partida (25 min):** os anjos do spawn somem e quem ainda estiver como fantasma ressuscita no spawn. As forjas
   acendem (skybeam). Clicar numa forja dá o orbe: efeito da presença de DK no portador, escala, o feixe da forja apaga, o
   jogador é desmontado e perde stealth/invisibilidade.
   O buff Berserk aparece no lugar do portal para o andar de cima (2383.65, -5645.20, 420.77, a 0,2 jarda do eixo porta →
   forja Unholy, então a mesma distância para os dois times), virado para o poço. É o GO 990004, cópia do 179905 das
   BGs com trap tipo 1: o GO lança a spell e se desativa. Um GO novo também começa desativado, então o script só o
   considera usado depois de vê-lo pronto; aí o apaga e cria outro 180 s depois. As BGs fazem isso no
   `Battleground::HandleTriggerBuff`, que não roda fora delas.
6. **Tick de 5 s:** pontos por portador conforme a zona. Atualiza os world states.
7. **Morte:** o orbe volta à forja (com anúncio). A morte conta no placar só depois da preparação. O Release leva o
   fantasma ao anjo da ponta do time, e só fantasmas ressuscitam na onda de 30 s.
8. **Fim:** 1600 pontos ou fim do tempo. Placar final com vencedor e todos ficam parados, como numa BG
   (`SetClientControl`, que o cliente desfaz sozinho no teleporte). Quem volta de um logout nesse período recebe o
   placar e também fica parado. Após 2 min (ou pelo botão Leave), todos voltam à
   posição salva e recuperam phase, escala, auras e PvP.
9. **Logout:** o orbe cai antes do save. O jogador fica marcado como offline por até 300 s; se voltar a tempo,
   continua na partida. Se não, ou se a partida já acabou, o registro de retorno fica no banco.
10. **Login sem partida (queda do servidor ou offline expirado):** o retorno é agendado e executado pelo update do
    mundo 1 s depois, com até 10 tentativas. O registro só é apagado depois de confirmar a chegada. Todo login remove
    auras de orbe que tenham ficado salvas.

Durante a partida, para os participantes: a aura de zona `51915` (Undying Resolve, impede morrer) é removida, o
santuário do mapa 609 é desligado e a flag PvP é forçada.

## Visual

| Orbe | Feixe na forja (aura) | Ao pegar (visual kit, uma vez) | Aura no portador |
|---|---|---|---|
| Frost | 62893 Blue Skybeam | 10288, da Frost Presence (48263) | 55840 Blue Wyrmrest Warden Beam |
| Blood | 62894 Red Skybeam | 10283, da Blood Presence (48266) | 55824 Red Wyrmrest Warden Beam |
| Unholy | 62895 Green Skybeam | 10297, da Unholy Presence (48265) | 55838 Green Wyrmrest Warden Beam |

Todas as auras são dummy (sem efeito de stat) e ficam com duração infinita. As presenças não são aplicadas: têm efeito
de stat (armadura, ameaça, dano, haste) e trocariam a presença de um DK. O script só toca o efeito delas
(`SendPlaySpellVisualKit`, o `ImpactKit` do `SpellVisual.dbc`) uma vez ao pegar o orbe; esses kits não têm versão
permanente (`StateKit`) e trazem som, então repeti-los tocaria o som o tempo todo. Os modificadores de dano e cura
ficam no `UnitScript` (`ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyPeriodicDamageAurasTick`, `OnHeal`), e
`OnDamage`/`OnHeal` também alimentam as estatísticas do placar final.

## Configuração (`worldserver.conf`, UTF-8 sem BOM)

```
AcherusOrbs.PlayersPerTeam = 10
AcherusOrbs.MinPlayersPerTeam = 10
AcherusOrbs.KillBonus = 10
```

As chaves não estão no `worldserver.conf.dist`. Sem elas, os valores padrão acima são usados (o log avisa).
São lidas no `OnStartup` e no `.reload config`.

## Comandos GM (permissão `RBAC_PERM_COMMAND_DEBUG`)

| Comando | Efeito |
|---|---|
| `.acherus queue` | coloca/tira o jogador selecionado (ou você) da fila |
| `.acherus start` | inicia uma partida com quem está na fila, ignorando o mínimo |
| `.acherus begin` | pula a preparação da sua partida, como o `.bg start`; fora de uma partida (ou no console), de todas as partidas em preparação |
| `.acherus stop` | encerra todas as partidas como empate |
| `.acherus status` | partidas, phase, tempo, placar e tamanho das filas |

GMs com `.gm on` veem todas as phases. Para jogar uma partida, use `.gm off`.

## Build

```powershell
# só scripts (rápido)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target scripts -- /m:2 /p:CL_MPCount=3
# core (mudanças em ScriptMgr.h recompilam quase toda a game.lib)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target game -- /m:2 /p:CL_MPCount=3
# link: o worldserver precisa estar parado (senão LNK1104)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target worldserver -- /m:2 /p:CL_MPCount=3
```

Arquivos novos em `Custom/` exigem reconfigurar (`cmake C:\TrinityBuild335`). Não rode dois builds ao mesmo tempo na
mesma pasta: `cl.exe` órfãos travam o PCH (`MSB6003 ... cmake_pch.pch`).

## Estado

Validado em jogo:
- fila pelo NPC (gossip com saudação, regras, entrar/sair) e pelo comando;
- teleporte, phase, placar do EotS, preparação;
- forjas, orbes, visuais, pontuação;
- PvP entre facções;
- anjos e ressurreição nas pontas;
- placar final e saída;
- preparação estilo Warsong (anjo no spawn + Release + onda);
- contagem por notificação no lugar do frame nativo;
- reconexão em até 300 s e orbe solto antes do save;
- retorno pós-queda pelo update do mundo, com confirmação;
- exigência de nível 80;
- timers do placar final via battlefield status ("Time Elapsed" e "closing in");
- coluna Flag Captures com os pontos de cada jogador;
- jogadores parados durante o placar final e teleporte de saída no tempo certo.

Implementado e compilado, **ainda não testado em jogo**:
- entrada em partida em andamento (vagas por facção);
- raid por time, com devolução do grupo original na saída;
- +10 pontos por kill de inimigo (contam na coluna de pontos de quem deu o golpe final); vitória por 1600 checada no
  update do mundo, já que a kill acontece no thread do mapa.
- comando `.acherus begin` (pular a preparação);
- efeito da presença de DK uma vez ao pegar o orbe, depois aura fixa (Frost 55840, Blood 55824, Unholy 55838);
- sons, mensagens de orbe no centro, Berserk e vitória validados; falta validar a cor amarela de "+N points" e da
  contagem do anjo (`SendAreaTriggerMessage`), o Berserk virado para o poço e o 51721 para todos.

## Pendências

- Testar a entrada em partida em andamento: um terceiro jogador na fila deve cair na partida, na preparação e depois
  dela, sem passar do limite do time.
- Testar o raid: criado com o time, status PvP dos membros, volta ao grupo original na saída e no fim, religação após
  logout, /leave e convite de alguém de fora corrigidos em até 1 s.
- Testar "+N points" e a contagem do anjo em amarelo, o Berserk virado para o poço e a velocidade de 51721 para todos
  (e que ela some de todos ao sair, DKs inclusive).
- Decidir se as forjas acendendo tocam som (proposta: 8232, `BG_WS_SOUND_FLAGS_RESPAWNED`).
- NPC de fila para a Horda (hoje só em Old Town). Eventual entrada pelo "Random Battleground" da UI.
- Adicionar as chaves de configuração ao `worldserver.conf.dist` ao integrar no servidor de destino (no módulo, sem elas valem os padrões).
