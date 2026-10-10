# Heart of Acherus

Battleground customizado no estilo **Temple of Kotmogu**, jogado em cópias "phaseadas" do salão central de
**Acherus: The Ebon Hold** (mapa 609). Roda na branch `3.3.5` (cliente 3.3.5a, build 12340) **sem patch de cliente**:
todo o comportamento é server-side e reaproveita só dados que o cliente já conhece.

- Branch: `feature/acherus-orbs` (nome histórico, criada a partir de `3.3.5-local`)
- Worktree: `.worktrees/3.3.5`
- Build dir: `C:\TrinityBuild335` (Visual Studio 17 2022, `RelWithDebInfo`)

Documentação complementar:

- [`docs/client-ui.md`](docs/client-ui.md): interface sem patch (disfarce de Eye of the Storm) e o protocolo
  Warden/addon do Lua de UI.
- [`docs/visuals.md`](docs/visuals.md): visuais das forjas, do portador, do domo, da barreira da escada e do livro,
  com a pesquisa de DBC.

## Regras

| Regra | Valor | Origem |
|---|---|---|
| Times | 10v10, Aliança x Horda | warcraft.wiki.gg |
| Vitória | primeiro a 1600 pontos | warcraft.wiki.gg |
| Duração | 25 min, depois vence o maior placar (igual = empate) | decisão do projeto |
| Tick | a cada 5 s, por runa carregada | script do Kotmogu no TrinityCore `master` |
| Pontos por tick | poço central **6**, plataforma das forjas **4**, fora do salão e andar de cima **2** | valores do retail (script do `master`); a wiki cita 5/4/3 |
| Buff do portador | por acúmulo: +20% dano causado, +20% dano recebido, -10% cura recebida; +1 acúmulo a cada 15 s; tetos de +100%/+100%/-50% | decisão do projeto (inspirado na 121164, Orb of Power) |
| Escala do portador | +20% por acúmulo, até 2x (+100%), travando em 5 acúmulos | decisão do projeto (sem fonte) |
| Runas | 1 por jogador; morte ou saída do portador devolve a runa à forja; portador não monta | warcraft.wiki.gg |
| Kill | +10 pontos para o time a cada kill de jogador inimigo, portador ou não (a spell 112910 do bônus não existe no 3.3.5) | decisão do projeto |
| Preparação | 2 min, cada time preso ao seu spawn, dentro de um domo com paredes invisíveis | duração: padrão de BG do core; domo: decisão do projeto, inspirado nas barreiras do Eye of the Storm |
| Ressurreição | ondas de 30 s, só para quem deu "Release Spirit" e está no range do guide; 2 cemitérios por facção no andar de cima, vale o mais próximo | padrão de BG do core; mais próximo como `BattlegroundAB::GetClosestGraveyard` |
| Desconexão | lugar mantido por 300 s | `MAX_OFFLINE_TIME` das BGs |
| Nível | apenas 80 | decisão do projeto |
| Buffs | 2 Berserk (spell 23505), um por facção, de volta 3 min depois de pegos | posição: decisão do projeto; tempo: `BUFF_RESPAWN_TIME` das BGs |
| Montaria | conforme `HeartOfAcherus.MountMethod`: 0 nenhuma no salão; 1 botão que monta no Acherus Deathcharger (48778) | decisão do projeto |
| Velocidade | 51721 (Dominion over Acherus, +75%, `spell_area` da quest 12657) removida de todos durante a partida | decisão do projeto |

O -5% de absorção recebida do Orb of Power não foi implementado: o 3.3.5 não tem hook para absorções.

## Layout (mapa 609)

Toda posição por facção é espelhada no **eixo dos spawns** (a mediatriz dos dois spawns, que passa pela porta).
As coordenadas ficam em `Positions` e `RuneTemplates` (`HoALayout.*`).

| Ponto | Posição |
|---|---|
| Centro (poço) | 2459.4, -5593.4, 414.1 (circuncentro das 3 forjas) |
| Forja **Frost** | 2493.37, -5642.43, 420.86 |
| Forja **Unholy** (oposta à porta) | 2509.31, -5560.39, 420.86 |
| Forja **Blood** | 2427.28, -5544.45, 420.86 |
| Porta | 2410.68, -5626.74, 420.66 |
| Spawn Aliança / Horda | 2447.56, -5656.40 / 2397.17, -5581.70 (z 420.65) |
| Cemitérios Aliança (andar de cima) | 2438.10, -5707.64 e 2574.63, -5615.65 (z 444.6) |
| Cemitérios Horda (andar de cima) | 2346.12, -5571.28 e 2482.54, -5479.14 (z 444.6) |
| Portal para o andar de cima | 2383.65, -5645.20, 420.77 → 2517.90, -5554.81, 444.12 |
| Berserk | 2472.58, -5530.56 e 2523.22, -5605.63 (z 420.65) |

Zonas de pontuação (`GetPointsForPosition`): poço = raio 25 e z < 418; plataforma = raio 62 e até 56 jardas na direção
da porta; o resto (corredor, área externa, andar de cima) = fora.

## Arquitetura

### Arquivos

O módulo fica em `src/server/scripts/Custom/Battlegrounds/HeartOfAcherus/`; outras BGs customizadas entram ao lado,
em `Custom/Battlegrounds/<Nome>/`. Nomes seguem o próprio 3.3.5 (como `Battlefield/BattlefieldWG.cpp` +
`battlefield_script_loader.cpp`): classes do modo em PascalCase com prefixo `HoA`, scripts em snake_case com `hoa`
(`cs_` para comandos). O prefixo evita que um header genérico (`Util.h`) seja pego no lugar do header do core.

| Arquivo | Responsabilidade |
|---|---|
| `HoADefines.h` | constantes: IDs, spells, kits, sons, world states, timers, pontuação, `RunePower`, enums (`Rune`, `MatchStatus`, `RemoveMode`, `MountMethod`) |
| `HoALayout.h/.cpp` | `Positions`, tabela `RuneTemplates` (forja, visuais, auras do portador por runa), zonas de pontuação |
| `HoAMgr.h/.cpp` | `Manager` (`sHeartOfAcherusMgr`): config, update do mundo, matchmaking, phases, entrada/saída de jogadores, login/logout/retorno, entrada de todos os hooks |
| `HoAQueue.h/.cpp` | `MatchQueue`: fila por facção, elegibilidade, slot do status "queued" |
| `HoAMatch.h/.cpp` | `Match`: preparação → batalha → fim, checagem de 1 s, pontuação, kills, anúncios, frames da partida |
| `HoARunes.h/.cpp` | `MatchRunes`: uso da forja, pegar/soltar, acúmulos, modificadores de dano/cura, trava da forja vista de cima, marcadores do minimapa |
| `HoAHall.h/.cpp` | `MatchHall`: tudo que a partida spawna no salão (forjas e visuais, domo/paredes/livro, barreira da escada, portal, Berserk, criaturas ambiente) |
| `HoAGraveyards.h/.cpp` | `MatchGraveyards`: spirit guides, release, fila e ondas de ressurreição |
| `HoARaids.h/.cpp` | `MatchRaids`: raid de battlefield por time |
| `HoABattlegroundUI.h/.cpp` | pacotes do disfarce de Eye of the Storm (world states, placar, battlefield status), sem estado |
| `HoAClientUI.h/.cpp` | `ClientUI`: listener via Warden, entrega das partes Lua, probe/resync, comandos do cliente, relabel, erros |
| `HoAUtil.h/.cpp` | spawn de GO/criatura por partida, aura permanente, desmontar, reviver |
| `hoa_scripts.cpp` | NPC da fila, forja clicável, `PlayerScript`, `UnitScript`, `WorldScript`, `AddSC_heart_of_acherus` |
| `cs_hoa.cpp` | comandos `.acherus` |
| `client/hoa_login.lua` | parte 1 do Lua de UI (todos os jogadores) |
| `client/hoa_match.lua` | parte 2 do Lua de UI (fila e partida) |
| `conf/heart_of_acherus.conf.dist` | bloco de configuração para o `worldserver.conf` |
| `sql/custom/world/2026_10_01_00_world_heart_of_acherus.sql` | NPC 990000, gossip 990000/990001, forjas 990001–990003, Berserk 990004, parede 990005, poça 990006, livro 990007 e páginas 990000/990001, auras 990008/990009 |
| `sql/custom/world/2026_10_04_00_world_heart_of_acherus_spirit_healer.sql` | `spell_area` da área 4342 que libera 2584/22012/44535 no Acherus |
| `sql/custom/characters/2026_10_01_00_characters_heart_of_acherus.sql` | tabela `custom_heart_of_acherus_return` |

Os SQLs são aplicados pelo updater do worldserver (`updates_include` já aponta para `sql/custom`).
**Atenção:** `sql/custom/*/.gitignore` ignora `*.sql`, então eles só entram num commit com `git add -f`.

### Composição

```
Manager (sHeartOfAcherusMgr)
├── Settings, MatchQueue, ClientUI
└── Match (uma por phase)
    ├── MatchHall        objetos do salão
    ├── MatchRunes       runas e portadores
    ├── MatchGraveyards  guides e ondas
    └── MatchRaids       raids dos times
BattlegroundUI (pacotes), Layout (dados), Util (spawns)
```

Os hooks entram pelo `Manager`, que acha a partida do jogador e delega ao componente. Para estender:

- **runa/forja nova ou visual de forja:** `RuneTemplates` e `Ids`/`Spells`;
- **objeto novo no salão:** `MatchHall` (com `SpawnGameObject`/`SummonCreature` do `Util`);
- **método de montaria novo:** valor em `MountMethod` + lado cliente na parte 2 do Lua;
- **mensagem nova do cliente:** `ClientUI::RegisterCommand` (como o `"mount"`).

### Hooks adicionados ao core

Todos são `ScriptMgr` opt-in, sem efeito quando nenhum script os usa. O core não menciona o módulo.

| Hook | Onde é chamado | Para quê |
|---|---|---|
| `OnSendInitWorldStates` | `Player::SendInitWorldStates`, antes do envio | trocar mapa/zona do pacote pelos do Eye of the Storm e mostrar o placar |
| `OnLeaveBattlefield` | `HandleBattlefieldLeaveOpcode` | botão "Leave Battleground" do placar final |
| `OnJoinBattlegroundQueue` | `HandleBattlemasterJoinOpcode`/`HandleBattlemasterJoinArena` | sair da fila do Acherus ao entrar numa fila real |
| `OnCheckSanctuary` | `Player::UpdateArea` | o mapa 609 inteiro é santuário (`AreaTableEntry::IsSanctuary`), o que impede PvP |
| `OnCheckOutdoors` | `Spell::CheckCast` e `Player::CheckAreaExplore` | o salão conta como ambiente aberto na partida, para montar |
| `OnRepopAtGraveyard` | início de `Player::RepopAtGraveyard` | mandar o fantasma ao cemitério do time, e não ao dos DKs |
| `OnSpiritHealerQuery` | clique no guide (`NPCHandler`) e `CMSG_AREA_SPIRIT_HEALER_QUERY`, fora de BG/Battlefield | responder `SMSG_AREA_SPIRIT_HEALER_TIME` (timer do popup nativo) |
| `OnSpiritHealerQueue` | `CMSG_AREA_SPIRIT_HEALER_QUEUE`, fora de BG/Battlefield | entrar na fila da onda de ressurreição |
| `OnBeforeLogout` | `WorldSession::LogoutPlayer`, **antes do save** | soltar a runa antes de o personagem ser salvo |
| `OnPVPLogDataRequest` | `HandlePVPLogDataOpcode`, fora de BG/Battlefield | placar ao vivo sob demanda |
| `OnRequestBattlefieldStatus` | fim de `HandleRequestBattlefieldStatusOpcode` | manter o botão de BG no minimapa e detectar `/reload` |
| `OnBattlefieldPort` | topo de `HandleBattleFieldPortOpcode` | "Leave Queue" da janela PvP |
| `OnAddonMessage` | `HandleChatMessage`, mensagens `LANG_ADDON` | acks, probe, `PLAYER_LOGOUT` e comandos do Lua de UI |
| `OnWardenLuaExecuted` | `WardenWin::HandleCheckResult`, resposta a um `SendLua` | seguir o bootstrap do listener |

Além dos hooks, o core ganhou `Warden::SendLua` (envia um Lua sob demanda, fora do agendador de checks).

### Portabilidade

O módulo vai para outro servidor (wow-brasil, com core próprio), então mudanças no core ficam no mínimo e nunca
alteram o comportamento padrão. Além dos hooks, o módulo usa só APIs comuns aos cores 3.3.5 derivados do
TrinityCore: `Group::SetBattlefieldGroup`, `Player::SetBattlegroundOrBattlefieldRaid`, `SetClientControl` e os
pacotes de BG (`MSG_PVP_LOG_DATA`, `SMSG_BATTLEFIELD_STATUS`, `SMSG_INIT_WORLD_STATES`).

### Raids

Cada time de cada partida tem um raid do tipo "battlefield", sem mudança no core: o `Group` só testa se o ponteiro do
battlefield é nulo, então o módulo passa uma subclasse vazia de `Battlefield` (`RaidAnchor`) que nunca é registrada
nem atualizada. O raid não é salvo, guarda o grupo original de cada membro e o devolve na saída, aceita 1 membro e
mostra o status PvP. A checagem de 1 s religa quem voltou de um logout, readiciona quem deu /leave e tira quem não é
do time. Só o GUID do grupo é guardado (o `Group` pode se desfazer dentro do `RemoveMember`). O /bg não funciona
(exige um `Battleground`); o /raid funciona.

### Threads

- A lógica roda em `WorldScript::OnUpdate`, depois do update de todos os mapas (como Battlefield/Wintergrasp):
  teleportes e spawns entre mapas são seguros ali.
- O índice jogador → partida e a lista de partidas só mudam no update do mundo. Os hooks de mapa (dano, cura, forja,
  kill) leem esses dados ou alteram a própria partida, que fica toda no mapa 609.
- `MatchQueue` e `ClientUI` têm mutex próprio. Pedidos vindos de outros threads (Leave, `.acherus begin/stop/start`)
  são enfileirados e processados no update do mundo.
- Login e retorno pós-queda rodam no thread do mundo.

### Phasing

- Cada partida usa um bit livre de 9 a 31; Acherus usa os bits 1–256 nas quests de DK, então há **até 23 partidas
  simultâneas**.
- Na phase da partida os NPCs e GOs originais de Acherus somem, por isso cada partida spawna os seus (`MatchHall`,
  `MatchGraveyards`).
- A phase é reaplicada a cada segundo (auras de phase de quest voltam em troca de área) e, ao sair, recalculada como
  em `AuraEffect::HandlePhase`.
- Forjas, seus visuais, domos, portal e Berserk são vistos de longe (`VisibilityDistanceType::Large`, 200 jardas),
  só esses objetos.

## Fluxo da partida

1. **Fila:** NPC `990000` (`.npc add 990000`; hoje em Old Town, Stormwind), `.acherus join` ou o "Join" da linha
   "Heart of Acherus" na aba Battlegrounds da janela PvP (o botão "Join as Group" enfileira a party/raid, que entra no
   mesmo match). Exige nível 80 e nenhuma BG/arena nem fila real. Filas por facção. Na fila o jogador recebe um status
   "queued" falso (botão no minimapa, "Leave Queue" funciona). Entrar numa fila real tira o jogador da fila do Acherus.
2. **Início:** com o mínimo de cada lado (`MinPlayersPerTeam`) ou `.acherus start`, a partida pega até
   `PlayersPerTeam` jogadores elegíveis por fila (vivos, fora de combate, de voo e de instância); um grupo é sempre
   levado inteiro ou espera. Antes de criar uma partida, a fila preenche as vagas das partidas abertas, contando os
   offline dentro dos 300 s; quem entra com a batalha começada vai direto ao spawn.
3. **Entrada:** a posição vai para `custom_heart_of_acherus_return`; o jogador desmonta, recebe a phase e vai ao
   spawn do time, onde entra no raid do time.
4. **Preparação (2 min):** domo com paredes em cada spawn, livro de instruções e spirit guide no spawn (como no
   Warsong Gulch). Avisos aos 60 s e 30 s. As forjas já brilham, mas as runas só saem na batalha.
5. **Batalha (25 min):** somem domo, paredes, livro e guides do spawn; fantasmas ressuscitam no spawn. Aparecem o
   portal para o andar de cima e os 2 Berserk. Clicar numa forja dá a runa: efeito da presença de DK, auras e escala;
   o portador desmonta e perde stealth/invisibilidade. Do andar de cima as forjas não podem ser usadas.
6. **Tick de 5 s:** pontos por portador conforme a zona; world states atualizados.
7. **Morte:** a runa volta à forja (com anúncio). O Release leva ao cemitério mais próximo do time; a onda de 30 s
   revive quem está no range do guide. Fantasma no salão sobe pelo portal.
8. **Fim:** 1600 pontos ou tempo esgotado. Placar final com vencedor e todos parados (`SetClientControl`). Após
   2 min, ou pelo Leave, todos voltam à posição salva e recuperam phase, escala, auras e PvP.
9. **Logout:** a runa cai antes do save; o lugar fica guardado por 300 s. Depois disso, ou com a partida acabada, o
   retorno fica no banco.
10. **Login sem partida:** o retorno é feito pelo update do mundo, com até 10 tentativas, e o registro só é apagado
    após confirmar a chegada. Todo login remove auras de runa salvas.

Durante a partida, para os participantes: a aura `51915` (Undying Resolve) é removida, o santuário do mapa 609 é
desligado e a flag PvP é forçada.

## Configuração

Bloco documentado em [`conf/heart_of_acherus.conf.dist`](conf/heart_of_acherus.conf.dist), para colar no
`worldserver.conf` (UTF-8 sem BOM). Sem as chaves valem os padrões. Lidas no `OnStartup` e no `.reload config`.

| Chave | Padrão |
|---|---|
| `HeartOfAcherus.PlayersPerTeam` | 10 |
| `HeartOfAcherus.MinPlayersPerTeam` | 10 |
| `HeartOfAcherus.KillBonus` | 10 |
| `HeartOfAcherus.MountMethod` | 0 |
| `HeartOfAcherus.ClientUI` | 0 |
| `HeartOfAcherus.ClientLoginLuaFile` | vazio |
| `HeartOfAcherus.ClientMatchLuaFile` | vazio |

## Comandos

`.acherus join` usa `RBAC_PERM_JOIN_NORMAL_BG` (todo jogador tem), porque é o "Join" da janela PvP; `.acherus queue` e
os demais usam `RBAC_PERM_COMMAND_DEBUG`.

| Comando | Efeito |
|---|---|
| `.acherus join [group]` | o "Join" da aba Battlegrounds: enfileira você; com `group`, a party/raid inteira (mesmo match); idempotente |
| `.acherus queue` | GM: coloca/tira o jogador selecionado (ou você) da fila |
| `.acherus start` | inicia uma partida com quem está na fila, ignorando o mínimo |
| `.acherus begin` | pula a preparação da sua partida (como `.bg start`); fora de uma, de todas |
| `.acherus stop` | encerra todas as partidas como empate |
| `.acherus status` | partidas, phase, tempo, placar e filas |

GMs com `.gm on` veem todas as phases; para jogar, `.gm off`.

## Build e instalação

```powershell
# arquivos novos em Custom/ exigem reconfigurar
cmake C:\TrinityBuild335
# só scripts (rápido)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target scripts -- /m:2 /p:CL_MPCount=3
# core (mudanças em ScriptMgr.h recompilam quase toda a game.lib)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target game -- /m:2 /p:CL_MPCount=3
# link: o worldserver precisa estar parado (senão LNK1104)
cmake --build C:\TrinityBuild335 --config RelWithDebInfo --target worldserver -- /m:2 /p:CL_MPCount=3
```

Não rode dois builds ao mesmo tempo na mesma pasta: `cl.exe` órfãos travam o PCH (`MSB6003 ... cmake_pch.pch`).

### Migração do nome antigo (AcherusOrbs)

Bancos de desenvolvimento criados antes da renomeação (2026-10-09):

1. No `worldserver.conf`, trocar o prefixo `AcherusOrbs.` por `HeartOfAcherus.`, `ClientUi` por `ClientUI`,
   `ClientLuaFile` por `ClientLoginLuaFile` e os caminhos para `client/hoa_*.lua`.
2. No banco `characters`, mover os retornos pendentes:
   ```sql
   CREATE TABLE IF NOT EXISTS custom_heart_of_acherus_return LIKE custom_acherus_orbs_return;
   INSERT IGNORE INTO custom_heart_of_acherus_return SELECT * FROM custom_acherus_orbs_return;
   DROP TABLE custom_acherus_orbs_return;
   ```
3. O updater aplica os SQLs renomeados sozinho (ScriptNames novos) e limpa as referências antigas.

## Estado

Validado em jogo:
- fila pelo NPC e pelo comando; teleporte, phase, placar do EotS, preparação com domo, paredes e reserva;
- forjas, runas, visuais, pontuação, PvP entre facções, placar final e saída;
- preparação estilo Warsong, ressurreição em ondas, reconexão em 300 s, retorno pós-queda, nível 80;
- timers do placar final, coluna de pontos, jogadores parados no fim, efeito da presença;
- portal para o andar de cima, criaturas ambiente, livro de instruções (título, ícones, texto), botão de montaria.

Implementado, **ainda não testado em jogo**:
- entrada em partida em andamento; raid por time; bônus de kill; `.acherus begin`;
- 2 cemitérios por facção (o mais próximo); spirit guide exatamente no spawn na preparação;
- forjas travadas vistas do andar de cima (cursor e erro em vermelho);
- 2 Berserk espelhados; barreira da escada;
- este refactor (renomeação + divisão em componentes): compila; precisa de um teste de regressão completo.

## Pendências

- Teste de regressão do refactor: fila (NPC e janela PvP), `.acherus start/begin/status/stop`, preparação, cada runa
  (visual da forja, aura relabelada, acúmulos, escala, marcadores), tick e kill, morte/release/onda, portal e trava
  das forjas, Berserk, montaria, fim/placar/Leave, logout+login, `/reload`.
- Testar entrada em partida em andamento e o raid (grupo original devolvido, /leave e convite de fora corrigidos).
- Deixar um fantasma no guide por várias ondas com o popup nativo `AREA_SPIRIT_HEAL`.
- Decidir se as forjas acendendo tocam som (proposta: 8232, `BG_WS_SOUND_FLAGS_RESPAWNED`).
- NPC de fila para a Horda (hoje só em Old Town).
- Apagar os dados de teste locais 9909xx (livros/páginas do mapa 25).
- Ao integrar no servidor de destino: colar `conf/heart_of_acherus.conf.dist` no `worldserver.conf`.
