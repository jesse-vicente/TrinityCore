# Heart of Acherus: interface sem patch de cliente

O cliente 3.3.5 não conhece o modo, então o módulo reaproveita frames nativos fingindo ser o **Eye of the Storm**
(`HoABattlegroundUI`) e, opcionalmente, envia um Lua de UI que troca os rótulos e adiciona o que falta
(`HoAClientUI` + `client/*.lua`). Os rótulos herdados do EotS ("Bases", nome no minimapa e na fila,
"Flag Captures") vêm de DBC/FrameXML do cliente e só mudam pelo Lua.

## Frames nativos (disfarce de Eye of the Storm)

- **Placar de topo:** o cliente escolhe os frames de world state pelo mapa/zona **informados no**
  `SMSG_INIT_WORLD_STATES`. Com mapa 566 / zona 3820 aparece o frame do EotS ("Bases: N Victory Points: N/1600"):
  `2749`/`2750` levam os pontos e `2752`/`2753` as runas de cada time. O teto de 1600 e os textos são do cliente.
- **Placar ao vivo:** o cliente pede o placar com `MSG_PVP_LOG_DATA`; fora de uma BG nada respondia. O hook
  `OnPVPLogDataRequest` entrega o placar atual sob demanda.
- **Placar final:** `MSG_PVP_LOG_DATA` com vencedor abre "Alliance/Horde Wins" com KB, mortes, HK, dano e cura.
- **Timers do placar:** "Time Elapsed" e "Battleground closing in" vêm de um `SMSG_BATTLEFIELD_STATUS` active
  (`StartTimer`/`ShutdownTimer`), enviado ao entrar, no início da batalha e no fim, num slot de fila livre, com o
  QueueID do EotS e o mapa 566; na saída o slot recebe status none. O `StartTimer` conta só a batalha. O cliente
  mostra os tempos sem segundos (menos de 1 min aparece vazio).
- **Coluna de pontos:** com esse status o cliente espera a coluna do EotS (Flag Captures): cada jogador manda 1 stat
  (senão aparece lixo de memória), com os pontos que fez para o time (ticks + kills). A parte 1 do Lua a renomeia
  "Points".
- **Ícone de BG no minimapa:** na fila, na preparação e na batalha. O cliente descarta o status enviado antes de
  terminar de carregar o mundo, por isso o módulo responde ao `CMSG_BATTLEFIELD_STATUS` e reenvia o active quando o
  jogador entra no mundo. Clicar abre o placar.
- **Spirit healer:** ao entrar no range do guide o cliente manda `CMSG_AREA_SPIRIT_HEALER_QUEUE`; o módulo responde
  `SMSG_AREA_SPIRIT_HEALER_TIME` e o popup nativo `AREA_SPIRIT_HEAL` mostra o contador. A onda revive só quem ainda
  está a 17 jardas do guide (raio `AREA_SPIRIT_HEALER_IN_RANGE` do cliente, medido em jogo), no lugar e com os
  visuais de `Battleground::_ProcessResurrect` (22012 no guide, 24171 + 6962 + 44535 no jogador). 2584/22012/44535
  só funcionam em BG/Wintergrasp (`SpellInfo::CheckLocation`), daí as linhas de `spell_area` da área 4342.
- **Mensagens de runa:** `CHAT_MSG_RAID_BOSS_EMOTE` (amarelo no centro e no chat), nome da runa colorido com `|c`.
- **Pontos:** não há texto de combate nativo para pontos customizados; quem pontua recebe "+N points"
  (`SendAreaTriggerMessage`).
- **Sons** (`PlaySoundToAll` das BGs): 8174 ao pegar/devolver uma runa, 3439 no início, 8455/8454 na vitória.

## Lua de UI

### Partes

| Parte | Guard no cliente | Conteúdo | Quando |
|---|---|---|---|
| **1, login** (`hoa_login.lua`) | `AcherusBG_UI` | entrada "Heart of Acherus" na lista de BGs, nome no minimapa/dropdown, rótulos do placar, aviso de `PLAYER_LOGOUT`, helpers da parte 2 | login, todos os jogadores |
| **2, match** (`hoa_match.lua`) | `AcherusBG_Part2` | auras do portador, marcadores das runas no minimapa, botão de montaria, livro de instruções | ao entrar na fila ou na partida |

A parte 2 estende a 1 pela tabela `AcherusBG_UI` (`TITLE`, `LocalizedAcherusName`, `IsInAcherusMatch`,
`IsRealEyeOfTheStorm`, `AcherusQueueStatus`, `Relabel`) e só se aplica com a 1 presente. O servidor coloca
`AcherusBG_MountMethod=N` no topo da parte 2.

O Lua **não casa texto em inglês**: o nome da BG vem de `GetBattlefieldStatus` (o mesmo nome localizado que o
minimapa e a lista mostram), o "Bases:" do topo é trocado pelo primeiro `<rótulo>:` de cada linha
(`AlwaysUpFrame<n>Text`), só dentro da partida (`IsInAcherusMatch`), e a coluna do placar vem de um wrapper de
`GetBattlefieldStatInfo` (ícone vazio: o cliente desenha só o número). O relabel só age com `AcherusBG_UI.active`,
ligado pelo servidor na fila e na partida, e nunca na EotS real (`IsRealEyeOfTheStorm`).

### Entrega

1. **Listener via Warden.** `Warden::SendLua` (core) manda um `LUA_EVAL_CHECK` sob demanda. O listener vai em dois
   evals, cada um abaixo do teto de 166 caracteres:
   ```lua
   -- eval 1 (107 chars)
   AcherusBG_Listener=AcherusBG_Listener or CreateFrame"Frame"AcherusBG_Listener:RegisterEvent"CHAT_MSG_ADDON"
   -- eval 2 (133 chars)
   AcherusBG_Listener:SetScript("OnEvent",function(_,_,p,m,_,s)if p=="AcherusBG"and s==UnitName"player"then pcall(loadstring(m))end end)
   ```
   Só roda mensagens com prefixo `AcherusBG` **enviadas pelo próprio jogador** (o servidor manda tudo como whisper
   do jogador para ele mesmo); o `pcall` engole mensagens malformadas. Requer `Warden.Enabled = 1`.
2. **Bootstrap** no login e quando um probe fica sem resposta. A resposta do Warden ao eval 2
   (`OnWardenLuaExecuted`) dispara a parte 1; a parte 2 segue do ack da 1 para quem está na fila/partida.
3. **Probe** (entrada na fila, entrada na partida, `CMSG_BATTLEFIELD_STATUS`, ping de 25 s):
   ```lua
   local f=0 if AcherusBG_UI then f=f+1 end if AcherusBG_Part2 then f=f+2 end SendAddonMessage('AcherusBG','return '..f,'WHISPER',UnitName'player')
   ```
   A resposta é a máscara das partes aplicadas; o servidor reenvia só o que falta. Sem resposta em 1 s, refaz o
   bootstrap. Um probe por vez; o ping já leva o `active=true` na mesma mensagem.
4. **Partes em blocos** de 190 bytes, cada um um long-string `[==[ ... ]==]` somado a um acumulador por parte
   (`AcherusBG_P1`/`AcherusBG_P2`). O Lua ignora a quebra de linha logo após `[==[`, então cada bloco começa com um
   `\n` artificial e o payload chega byte a byte. A última mensagem roda o acumulador; erro do `pcall` volta como
   `return -2` (login) ou `return -3` (match) e o servidor reenvia. Sucesso: o próprio payload manda `return 1`
   (parte 1) ou `return 3` (parte 2).
5. **Limites:** cooldown de 1,5 s por parte e para o bootstrap (pedidos no cooldown são refeitos quando ele
   termina); teto de 3 tentativas por parte, depois só loga (zera no ack, no `/reload` e no login).

**Acks por whisper para si mesmo**, que funciona sem guilda (o canal `GUILD` não envia nada para quem não tem
guilda). O servidor recebe `CHAT_MSG_WHISPER` `LANG_ADDON` no hook `OnAddonMessage`; os corpos são Lua válido
(`return N`), inofensivos se ecoados. Corpos registrados com `ClientUI::RegisterCommand` (hoje `"mount"`) vão para o
seu handler.

**`/reload`:** a parte 1 registra `PLAYER_LOGOUT` (logout e `/reload`) e manda `return -1`. O servidor limpa o
estado do cliente e marca o jogador; o próximo pedido dele decide: o `CMSG_BATTLEFIELD_STATUS` que o cliente manda ao
recarregar a UI (ou a entrada na fila/partida) faz o resync (novo bootstrap e partes); um logout real não pede mais
nada e é limpo no `OnLogout`.

**Por que o ack não sai do bootstrap:** o wrapper do Warden é `local S,T,R=SendAddonMessage,function() <code> end
...`; os inicializadores de `local` são avaliados antes de `S` existir, então o `<code>` não o enxerga.

**Arquivos:** lidos em binário no startup e no `.reload config`; o `\r` é removido (o cliente pode cortar mensagens
em CR/LF). Uma parte com `]==]` não é enviada.

**Limitações:** só Windows 3.3.5a build 12340 (o eval usa o endereço fixo de `FrameScript::Execute`), com Warden
ligado e sem bypass; sem isso o cliente fica com os rótulos do EotS, sem erro. O listener usa 165 dos 166
caracteres do eval (o wrapper do Warden e o `IdStr` ocupam o resto do pacote de 255).

**Bug conhecido do Warden (não é do módulo):** o eval de Lua do Warden do core provoca erros de Lua esporádicos e
cosméticos no console do cliente (`<string>:"?":1: '=' expected near ...`), por corrupção de buffer no módulo do
Warden do cliente ([TrinityCore#25361](https://github.com/TrinityCore/TrinityCore/issues/25361)). Afeta qualquer
check Lua do Warden, não derruba o cliente e os relabels funcionam.

## Recursos do Lua

- **Lista de BGs (aba Battlegrounds):** o `PVPBattlegroundFrame` lista os tipos do `BattlemasterList.dbc`, sem id
  livre. A parte 1 envolve `GetNumBattlegroundTypes`/`GetBattlegroundInfo` para expor uma entrada "Heart of Acherus"
  na posição 2 (depois da Random Battleground), com selo verde "NEW"; o update nativo a desenha e rola com a lista.
  Selecionada, mostra a lore e a arte da Random Battleground (`Interface\PVPFrame\PvpRandomBg`). "Join"/"Join as
  Group" mandam `.acherus queue`. O ícone de fila sai da linha do EotS e vai para a nossa. A entrada é reconhecida
  pelo nome exibido, não pelo índice (ambíguo quando a lista foi montada sem o wrapper).
- **Minimapa e dropdown:** o tooltip do botão de BG (via `MiniMapBattlefieldFrame_OnUpdate`) e o título do menu do
  clique direito trocam o nome localizado do EotS por "Heart of Acherus".
- **Auras do portador:** os Portal States (33338/33339/33340) são relabelados (ícone de DK, nome, tooltip com os
  totais dos acúmulos) na barra de buffs, target, boss, raid, tooltip do party e no FCT. Os três têm o mesmo ícone
  no DBC, então são reconhecidos pelo nome localizado. O combat log não é alterável por Lua.
- **Marcadores das runas no minimapa:** o servidor chama `AcherusBG_Runes.Update(...)` a cada 0,25 s com a posição do
  jogador e de cada runa (portador ou forja), em coordenadas de mundo, e o nome do portador do mesmo time. O cliente
  segue o portador aliado pelo unit do raid e a si mesmo por `GetPlayerMapPosition` (transformação mundo↔mapa fixa,
  do `WorldMapArea.dbc`), converte para o minimapa (span por zoom do Astrolabe, ajustado ao Acherus), gira com o
  `rotateMinimap` e prende os ícones na borda. Os ícones (presenças de DK, 18×18, recortados em texels inteiros) só
  são re-ancorados quando andam ≥1 px, para não cintilar, e ficam em `Minimap+1`, abaixo dos botões nativos.
- **Botão de montaria** (`MountMethod` 1): o cliente bloqueia montarias em ambiente fechado sem falar com o servidor.
  O botão `AcherusBGMountButton` (arrastável, posição num CVar, `/click` para bind) manda `"mount"`; o servidor checa
  as recusas usuais e lança o Acherus Deathcharger (48778) com o tempo de cast normal, ou desmonta. Os erros saem em
  vermelho (`UIErrorsFrame`), porque o cliente ignora resultados de cast que não iniciou. Com runa, morto, em
  combate ou em forma de druida/Ghost Wolf, o ícone fica cinza.
- **Erros em vermelho:** `ClientUI::ShowError` (forjas, montaria); sem o Lua, vira notificação.
- **Livro de instruções:** veja `docs/visuals.md`.
