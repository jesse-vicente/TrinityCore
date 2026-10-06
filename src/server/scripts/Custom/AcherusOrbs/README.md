# Battle for Acherus

Battleground customizado no estilo **Temple of Kotmogu**, jogado em cópias "phaseadas" do salão central de
**Acherus: The Ebon Hold** (mapa 609). Roda na branch `3.3.5` (cliente 3.3.5a, build 12340) **sem patch de cliente**:
todo o comportamento é server-side e reaproveita só dados que o cliente já conhece.

- Branch: `feature/acherus-orbs` (criada a partir de `3.3.5-local`)
- Worktree: `.worktrees/3.3.5`
- Build dir: `C:\Build` (Visual Studio 17 2022, `RelWithDebInfo`)

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
| Preparação | 2 min, cada time preso ao seu spawn, dentro de um domo com paredes invisíveis | duração: padrão de BG do core; domo: decisão do projeto, inspirado nas barreiras do Eye of the Storm |
| Ressurreição | ondas de 30 s, só para quem deu "Release Spirit" e está no range do guide | padrão de BG do core |
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
| `sql/custom/world/2026_10_01_00_world_acherus_orbs.sql` | NPC 990000, textos de gossip 990000/990001, forjas 990001–990003, buff Berserk 990004, parede invisível da preparação 990005 |
| `sql/custom/characters/2026_10_01_00_characters_acherus_orbs.sql` | tabela `custom_acherus_orbs_return` |
| `sql/custom/world/2026_10_04_00_world_acherus_orbs_spirit_healer.sql` | linhas de `spell_area` (área 4342) que liberam 2584/22012/44535 no Acherus |
| `client/acherus_orbs_ui.lua` | Lua de UI enviado ao cliente (relabels do EotS + marcadores das orbs no minimapa) |

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
| `OnSpiritHealerQuery` | clique no Spirit Guide (`NPCHandler`) e `CMSG_AREA_SPIRIT_HEALER_QUERY`, fora de BG/Battlefield | responder com o `SMSG_AREA_SPIRIT_HEALER_TIME` (timer do popup nativo) |
| `OnSpiritHealerQueue` | `CMSG_AREA_SPIRIT_HEALER_QUEUE`, fora de BG/Battlefield | entrar na fila de ressurreição do wave, mapeado ao guide (visual 2584) |
| `OnBeforeLogout` | `WorldSession::LogoutPlayer`, junto do `EventPlayerLoggedOut` das BGs, **antes do save** | soltar o orbe antes de o personagem ser salvo |
| `OnPVPLogDataRequest` | `HandlePVPLogDataOpcode` (`MSG_PVP_LOG_DATA`), fora de BG/Battlefield | responder ao pedido do placar com os dados atuais, deixando-o ao vivo durante a partida |
| `OnRequestBattlefieldStatus` | fim de `HandleRequestBattlefieldStatusOpcode` (`CMSG_BATTLEFIELD_STATUS`) | manter o botão de BG no minimapa respondendo o pedido de status do cliente |
| `OnBattlefieldPort` | topo de `HandleBattleFieldPortOpcode` (`CMSG_BATTLEFIELD_PORT`) | tratar o "Leave Queue" da janela PvP, que sai da fila do Acherus |
| `OnAddonMessage` | `ChatHandler::HandleMessagechatOpcode`, mensagens `LANG_ADDON` | receber o handshake do bootstrap do Warden e responder com o Lua de UI |
| `OnWardenLuaExecuted` | `WardenWin::HandleCheckResult`, quando o cliente responde a um `SendLua` | saber que o listener foi instalado e enviar o payload na hora |

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
  (`23837`) com o feixe de cada forja, Spirit Guides (`13116`/`13117`), o buff Berserk (990004) e, na preparação, o
  domo (NPC `28306`) e as paredes invisíveis (990005) de cada spawn.
- A phase de cada participante é reaplicada a cada segundo. Isso cobre auras de phase de quest, que podem ser
  reaplicadas por troca de área.
- Ao sair, a phase é recalculada como em `AuraEffect::HandlePhase`.
- As forjas, os bunnies dos feixes e os domos da preparação são vistos de longe: cada um recebe
  `SetVisibilityDistanceOverride(VisibilityDistanceType::Large)` (200 jardas; o mapa usa 100). O ajuste vale só para
  esses objetos e não mexe em `Visibility.Distance.*` nem em nada fora da partida.

### Interface sem patch de cliente

Testado em jogo com os comandos `.debug bgui`, que eram um patch local de `cs_debug.cpp` e não foram commitados:

- **Placar de topo:** o cliente escolhe os frames de world state pelo mapa/zona **informado no pacote**
  `SMSG_INIT_WORLD_STATES`. Mandando mapa 566 / zona 3820, aparece o frame do Eye of the Storm ("Bases: N Victory
  Points: N/1600"). Usamos `2749`/`2750` para os pontos e `2752`/`2753` (Bases) para os orbes que cada time segura.
  Os textos e o teto de 1600 são fixos do cliente. O rótulo "Bases" é do `WorldStateUI.dbc` do cliente (só mandamos o
  valor); renomear para "Orbs" exigiria patch de cliente.
- **Placar durante a partida:** o cliente pede o placar com `MSG_PVP_LOG_DATA` ao abrir a janela; fora de uma BG real
  nada respondia. O hook `OnPVPLogDataRequest` (`HandlePVPLogDataOpcode`) entrega o placar atual sob demanda, então os
  jogadores aparecem e a coluna de pontos por jogador atualiza enquanto a janela está aberta.
- **Placar final:** `MSG_PVP_LOG_DATA` com vencedor abre a tela "Alliance/Horde Wins" com KB, mortes, HK, dano e cura.
  Sem vencedor, nada aparece. Não há colunas extras.
- **Timers do placar:** "Time Elapsed" e "Battleground closing in" vêm de um `SMSG_BATTLEFIELD_STATUS` com status
  active (`StartTimer` e `ShutdownTimer`). O script manda esse pacote ao entrar na partida (como o
  `HandleBattleFieldPortOpcode` das BGs), de novo no início do combate e no fim, num slot de fila de BG que o jogador
  não está usando, com o QueueID do Eye of the Storm e o mapa 566. Na saída, o slot é limpo com status none. Durante a
  partida o `StartTimer` conta só o tempo decorrido de combate, sem a preparação. O cliente mostra os dois tempos sem
  segundos, então menos de 1 minuto aparece vazio. Com esse status, o cliente também passa a esperar a coluna do EotS
  (Flag Captures) no placar: cada jogador precisa mandar 1 stat, senão aparece lixo de memória. A coluna mostra os
  pontos que o jogador fez para o time (ticks como portador + bônus de kill). O cliente deixa o valor 0 em branco.
- **Spirit Healer:** funciona como nas BGs. Ao entrar no range do spirit guide com o ghost, o cliente manda
  `CMSG_AREA_SPIRIT_HEALER_QUEUE` (`AREA_SPIRIT_HEALER_IN_RANGE` → `AcceptAreaSpiritHeal()` + `StaticPopup_Show("AREA_SPIRIT_HEAL")`),
  o módulo responde com `SMSG_AREA_SPIRIT_HEALER_TIME` (o mesmo `TimeLeft` da BG) e o popup nativo `AREA_SPIRIT_HEAL` mostra
  o contador via `GetAreaSpiritHealerTime()`. O jogador entra na fila de ressurreição (`OnSpiritHealerQueue`) com o visual
  2584 "Waiting to Resurrect" e o wave de 30 s revive **só quem ainda está dentro do range do guide** (17 jardas, o raio
  `AREA_SPIRIT_HEALER_IN_RANGE` do cliente, medido em jogo), **no lugar** e com os visuais da BG
  (`Battleground::_ProcessResurrect`: 22012 no guide, 24171 + 6962 + 44535 no jogador). Quem sai do range não é revivido,
  e a aura 2584 é removida quando o jogador deixa a fila. O core restringe 2584/22012/44535 a BG/Wintergrasp
  (`SpellInfo::CheckLocation`), então o módulo depende das linhas de `spell_area` da área 4342
  (`2026_10_04_00_world_acherus_orbs_spirit_healer.sql`) para os casts passarem.
- **Ícone de BG no minimapa:** aparece na fila, na preparação e durante toda a partida. O cliente descarta o status
  enviado antes de terminar de carregar o mundo; por isso o módulo responde o `CMSG_BATTLEFIELD_STATUS` com o status
  fake (active ou queued) e reenvia o active assim que o jogador entra no mundo. Clicar no ícone abre o placar. O nome
  "Eye of the Storm" vem do `BattlemasterList.dbc` do cliente e não pode ser trocado sem patch.
- **Marcadores das orbs no minimapa:** ícones desenhados por Lua (payload do cliente) parentados ao `Minimap`, um por
  orb, com o ícone da presença de DK (Frost/Blood/Unholy) recortado via `SetTexCoord` (tira a borda clara embutida na
  textura). O servidor manda a posição de mundo do jogador e de cada orb (do portador, ou da forja se não portado) a
  cada 0,25 s pelo canal de addon messages (`AcherusBG_Orbs.Update`). O cliente reconstrói a posição do jogador a cada
  frame via `GetPlayerMapPosition` (transformação mundo↔mapa **fixa** no Lua, valores do `WorldMapArea.dbc` do cliente)
  para o movimento ficar suave, segue o unit token dos portadores do mesmo time e converte para o minimapa com o span
  de zoom (`MinimapSize` do Astrolabe, já ajustado ao Acherus), prendendo os ícones na borda.
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
   Exige nível 80 e o jogador não pode estar em BG, arena nem em fila de BG/arena real (`InBattlegroundQueue`). As
   filas são separadas por facção. Enquanto espera, o jogador recebe um battlefield status "queued" fake, então o
   botão de BG aparece no minimapa e o "Leave Queue" da janela PvP sai da fila.
   As duas filas são mutuamente exclusivas nos dois sentidos: se o jogador entrar numa fila real de BG/arena enquanto
   espera a Acherus, ele sai automaticamente da fila Acherus (hook `OnJoinBattlegroundQueue` no core limpa o status
   fake e o slot).
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
4. **Preparação (2 min):** cada time fica num domo no seu spawn (ver "Domo da preparação"), com avisos aos 60 s e
   30 s. As forjas já ficam acesas, mas os orbes só podem ser pegos quando a batalha começa. Os anjos ficam dentro do
   spawn, como no Warsong Gulch. Quem morre dá Release e ressuscita ali, na onda.
5. **Partida (25 min):** os anjos do spawn, o domo e as paredes somem, e quem ainda estiver como fantasma ressuscita
   no spawn. Clicar numa forja dá o orbe: efeito da presença de DK no portador, escala, o feixe da forja apaga, o
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
| Unholy | 62895 Green Skybeam + 60426 Ghost State | 10297, da Unholy Presence (48265) | 55838 Green Wyrmrest Warden Beam |

Todas as auras são dummy (sem efeito de stat) e ficam com duração infinita. As presenças não são aplicadas: têm efeito
de stat (armadura, ameaça, dano, haste) e trocariam a presença de um DK. O script só toca o efeito delas
(`SendPlaySpellVisualKit`, o `ImpactKit` do `SpellVisual.dbc`) uma vez ao pegar o orbe; esses kits não têm versão
permanente (`StateKit`) e trazem som, então repeti-los tocaria o som o tempo todo. Os modificadores de dano e cura
ficam no `UnitScript` (`ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyPeriodicDamageAurasTick`, `OnHeal`), e
`OnDamage`/`OnHeal` também alimentam as estatísticas do placar final.

A forja Unholy tem, além do feixe verde, a aura 60426 Ghost State (`Spells::ForgeAuraUnholy`), permanente no bunny
invisível que existe sobre cada forja (o mesmo que segura os feixes) enquanto o orbe está pronto. Um círculo do Scourge
(`SC_CastingCircle_01`, GO 191206) foi testado no lugar do feixe verde e descartado.

### Domo da preparação

- **Visual:** o domo do Anti-Magic Zone do DK. No DBC, o visual da 50461 é um kit de canalização (SpellVisual 11242):
  só aparece enquanto uma unidade "canaliza" a spell. O módulo cria o NPC `28306` (o totem do próprio AMZ) no centro
  de cada spawn e o deixa canalizando a 50461 (`SetChannelSpellId`, como os Spirit Guides fazem com o feixe deles),
  sem aplicar a aura. Escala 2 (o AMZ tem ~7 jardas na escala 1). O NPC fica amigável e passivo; jogadores não o veem,
  e GMs veem só um totem de ar pequeno. A versão 72628 "Anti-Magic Zone (Small)" foi descartada (escala 0.25).
- **Barreira:** octógono de 8 paredes invisíveis (GO 990005, modelo `CollisionWallPvP01` do 180322, ~11 jardas de
  largura na escala 1, usado como barreira nas BGs), tamanho 0.8, a 10 jardas do spawn, um pouco por dentro da borda do
  domo. Quem bloqueia é a colisão do cliente.
- **Reserva:** a checagem de 1 s devolve ao spawn quem passar de 13 jardas (atrás das paredes).
- O domo e as paredes somem quando a batalha começa (também com `.acherus begin`) e no fim da partida.

## Configuração (`worldserver.conf`, UTF-8 sem BOM)

```
AcherusOrbs.PlayersPerTeam = 10
AcherusOrbs.MinPlayersPerTeam = 10
AcherusOrbs.KillBonus = 10
AcherusOrbs.ClientUi = 0
AcherusOrbs.ClientLuaFile =
```

As chaves não estão no `worldserver.conf.dist` (que é do core); no ambiente de teste foram adicionadas ao fim do
`worldserver.conf`, numa seção "BATTLE FOR ACHERUS". Sem elas, os valores padrão acima são usados (o log avisa).
São lidas no `OnStartup` e no `.reload config`. `ClientLuaFile` (caminho do `.lua`) não tem padrão: com
`ClientUi = 1` e sem ele o relabel fica desligado.

## Relabel de UI no cliente (Warden::SendLua + addon messages)

Os rótulos que o cliente herda do Eye of the Storm ("Bases", o nome no minimapa/na fila, "Flag Captures")
vêm de DBC/FrameXML do cliente e não podem ser trocados pelo servidor. Para trocá-los sem distribuir
addon, o módulo instala um listener de `CHAT_MSG_ADDON` no cliente **via `Warden::SendLua`** (on demand,
fora do agendador de checks) e então envia o Lua de UI por addon messages:

1. `Warden::SendLua` (novo no core) envia um `LUA_EVAL_CHECK` único, cifrado, quando o módulo chama. O
   bootstrap do listener vai em **dois evals** (cada um abaixo do teto de 166 chars do Lua do Warden): o
   primeiro cria e registra o frame, o segundo instala o `OnEvent`. Prefixo `AcherusBG`, check de remetente
   e `pcall`:
   ```lua
   -- eval 1 (107 chars)
   AcherusBG_Listener=AcherusBG_Listener or CreateFrame"Frame"AcherusBG_Listener:RegisterEvent"CHAT_MSG_ADDON"
   -- eval 2 (133 chars)
   AcherusBG_Listener:SetScript("OnEvent",function(_,_,p,m,_,s)if p=="AcherusBG"and s==UnitName"player"then pcall(loadstring(m))end end)
   ```
   Requer `Warden.Enabled=1` e `AddonChannel=1`.
2. O listener só executa o Lua quando **o remetente é o próprio jogador** (`s==UnitName"player"`), porque
   o módulo envia tudo em nome do jogador (`Chat::Initialize` com `sender == player`). Outro jogador que
   mandar `AcherusBG` chega com o próprio nome e é ignorado. O `pcall` engole mensagens malformadas.
3. O `SendBootstrap` (Warden) é disparado **direto no login** (sessão nova). Nos demais gatilhos (entrada na
   fila, entrada na partida, `OnRequestBattlefieldStatus` — que cobre `/reload`) o módulo envia apenas um
   **probe** por addon message, barato:
   ```lua
   if AcherusBG_UI then SendAddonMessage('AcherusBG','return 2','GUILD')else SendAddonMessage('AcherusBG','return 1','GUILD')end
   ```
   - `return 2` → script já aplicado (nada a fazer).
   - `return 1` → listener presente, falta o payload → `OnAddonMessage` envia o script.
   - sem resposta em **1 s** → o listener está ausente → `SendBootstrap` (Warden); quando o cliente responde
     ao `SendLua`, o hook `OnWardenLuaExecuted` envia o eval 2 e, em seguida, o payload.
4. O payload (`client/acherus_orbs_ui.lua`) vai em blocos (prefixo `AcherusBG`), terminando com
   `loadstring(AcherusBG_Payload)()`.
   O cooldown do pedido é de 5 s. A recuperação após `/reload` vem do pedido de status do cliente
   (`OnRequestBattlefieldStatus`); há ainda um probe periódico de segurança (**25 s**) para quem está em
   partida/fila, que leva o refresh do `active` na mesma mensagem (probe + toggle combinados) e é
   rate-limitado (não reenvia enquanto um probe está em voo). Cooldown de bootstrap de **1.5 s**.
   Quando um pedido de bootstrap ou de payload chega durante o cooldown (ou quando `Warden::SendLua` não
   consegue enviar), o jogador entra numa fila de reenvio e o pedido é refeito no primeiro update após o
   cooldown expirar — sem depender do próximo probe.
   Cada bloco é um long-string `[==[ ... ]==]`; como o Lua **ignora a quebra de linha logo após o `[==[`**,
   o servidor prefixa cada bloco com um `\n` artificial — assim o `\n` ignorado é o nosso e o payload chega
   byte a byte, mesmo quando a fronteira do bloco cai num `\n` do arquivo (sem isso, um `\n` no início de um
   bloco é engolido e pode fundir duas linhas, quebrando a sintaxe do `loadstring(AcherusBG_Payload)`).
5. O `acherus_orbs_ui.lua` é idempotente (guarda `AcherusBG_UI`) e reaplica os relabels nos updates dos
   frames; ao terminar, o script manda `AcherusBG\treturn 2` e o servidor loga "applied the client UI script".
   O toggle de `AcherusBG_UI.active` (enviado pelo servidor nas transições de fila/partida) chama
   `AcherusBG_UI.Relabel()` no mesmo instante, então o relabel é aplicado imediatamente — sem esperar o
   próximo update de world state (que só acontece a cada 5 s na partida).

**Por que os corpos do handshake/ack são `return 1`/`return 2`:** as mensagens do modo são interceptadas
no hook `OnPlayerAddonMessage` (`ChatHandler.cpp`), que retorna antes do `Guild::BroadcastToGuild` — então
elas **não são ecoadas** para a guilda. Ainda assim os corpos são Lua válido (`return 1`/`return 2`),
porque um corpo cru (`R`/`DONE`) seria um erro de sintaxe caso alguma mensagem viesse a ser reenviada.

**Detalhes do `.lua`:** o arquivo é lido em binário e o `\r` é removido no servidor (o cliente pode cortar
mensagens de chat em CR/LF). O hook do tooltip do minimapa (`MiniMapBattlefieldFrame`) é feito de forma
preguiçosa dentro do `RelabelAll` (o botão pode não existir quando o payload roda) e usa `HookScript` no
`OnUpdate` do frame (o texto é reescrito a cada frame pelo `MiniMapBattlefieldFrame_OnUpdate` nativo),
trocando "Eye of the Storm" por "Battle for Acherus" em todas as linhas do tooltip.

**Por que o handshake não fica no bootstrap:** o wrapper do Warden é
`local S,T,R=SendAddonMessage,function() <code> end ...`; em Lua os inicializadores de `local` são
avaliados antes de `S` existir, então o `<code>` não enxerga esse `S` (referência vira global nil). Por
isso o aviso ao servidor sai pelo ping, não de dentro do bootstrap.

**`/reload`:** o servidor não detecta reload, mas o cliente pede o status da fila ao montar a UI; o
`OnRequestBattlefieldStatus` envia um probe, o timeout de 1 s dispara o bootstrap (cooldown 1.5 s), o listener
se reinstala e o payload é reenviado.

**Warden:** o bootstrap não usa o agendador de checks — o check 9900 de `warden_checks` foi **removido** e
o Warden fica na configuração padrão (`NumInjectionChecks=9`, `ClientCheckHoldOff=30`). O `SendLua` ocupa
um ciclo de check por envio (rápido, sob demanda).

**Limitações:** só Windows 3.3.5a build 12340 (o eval usa o endereço fixo `FrameScript::Execute`), com
Warden ligado e sem bypass; caso contrário o cliente fica com os rótulos do EotS, sem erro. O bootstrap
tem teto rígido: o código Lua não pode passar de 166 caracteres (o wrapper do Warden + `IdStr` ocupam os
outros 89 do pacote de 255); o listener atual usa 165 (entry de 254) para ficar com margem.

**Bug conhecido do Warden (não é do módulo):** o eval de Lua do Warden do core tem um bug em aberto do
TrinityCore ([issue #25361](https://github.com/TrinityCore/TrinityCore/issues/25361), Branch-3.3.5a) que
provoca **erros de Lua esporádicos e cosméticos no console do cliente** (`<string>:"?":1: '=' expected
near ...`), causados por **corrupção de buffer no módulo do Warden** do cliente — afeta qualquer check Lua
do Warden (inclusive os de anti-cheat 788/789/790), não só o nosso. Não há correção server-side confiável
(o upstream não conseguiu reproduzir de forma consistente). O erro não crasha o cliente e os relabels
funcionam normalmente; a única forma de evitá-lo seria distribuir um addon de cliente (descartado).

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
- jogadores parados durante o placar final e teleporte de saída no tempo certo;
- auras do portador (efeito da presença uma vez, depois Wyrmrest) e a Ghost State na forja Unholy;
- domo da preparação com paredes invisíveis e limitador de reserva;
- forjas acesas desde a preparação.

Implementado e compilado, **ainda não testado em jogo**:
- entrada em partida em andamento (vagas por facção);
- raid por time, com devolução do grupo original na saída;
- +10 pontos por kill de inimigo (contam na coluna de pontos de quem deu o golpe final); vitória por 1600 checada no
  update do mundo, já que a kill acontece no thread do mapa;
- comando `.acherus begin` (pular a preparação);
- botão de BG no minimapa na fila, na preparação e durante a partida (via `OnRequestBattlefieldStatus`), com o "Leave Queue" da janela PvP saindo da fila (`OnBattlefieldPort`), e placar ao vivo sob demanda (`OnPVPLogDataRequest`) com a coluna de pontos por jogador atualizando;
- regra de fila exclusiva: quem já está em fila de BG/arena real não entra na fila do Acherus (`InBattlegroundQueue`);
- relabel de UI no cliente via Warden (`OnAddonMessage` + `acherus_orbs_ui.lua`, sem check dedicado em `warden_checks`), validado em jogo (exige `Warden.Enabled=1`);
- sons, mensagens de orbe no centro, Berserk e vitória validados; falta validar a cor amarela de "+N points" e da
  contagem do anjo (`SendAreaTriggerMessage`), o Berserk virado para o poço e o 51721 para todos.

## Pendências

- Testar a entrada em partida em andamento: um terceiro jogador na fila deve cair na partida, na preparação e depois
  dela, sem passar do limite do time.
- Testar o raid: criado com o time, status PvP dos membros, volta ao grupo original na saída e no fim, religação após
  logout, /leave e convite de alguém de fora corrigidos em até 1 s.
- Testar "+N points" e a contagem do anjo em amarelo, o Berserk virado para o poço e a velocidade de 51721 para todos
  (e que ela some de todos ao sair, DKs inclusive).
- Confirmar a visão de longe das forjas e dos feixes (200 jardas).
- Decidir se as forjas acendendo tocam som (proposta: 8232, `BG_WS_SOUND_FLAGS_RESPAWNED`).
- NPC de fila para a Horda (hoje só em Old Town). Eventual entrada pelo "Random Battleground" da UI.
- Adicionar as chaves de configuração ao `worldserver.conf.dist` ao integrar no servidor de destino (no módulo, sem elas valem os padrões).
- Testar o relabel de UI no cliente: ligar `Warden.Enabled` e `AcherusOrbs.ClientUi`, apontar `ClientLuaFile` e ajustar os nomes de frame/função do `acherus_orbs_ui.lua` em jogo (foram escritos defensivos).
