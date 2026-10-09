# Heart of Acherus: visuais

Sem patch de cliente, todo visual é aura, kit ou modelo que o 3.3.5 já tem. Valores em `RuneTemplates`
(`HoALayout.cpp`) e constantes em `Spells`/`Ids` (`HoADefines.h`); spawns em `MatchHall`.

## Forjas

Enquanto a runa está na forja (também na preparação), a forja mostra auras em bunnies invisíveis (`23837`) e GOs de
cenário; tudo some quando a runa é pega e volta quando ela retorna (`MatchHall::SetForgeVisuals`).

| Forja | Bunny das auras (`ForgeAuraScale`) | Auras com bunny próprio (`ForgeScaledAuras`) | GOs (`ForgeObjects`) |
|---|---|---|---|
| Frost | 3x: 31954 Spirit Particles, super big | 32840 Beam (Blue) 2x; 58837 Icebound Fortitude 8x | — |
| Blood | 3x: 31951 Spirit Particles (red, super big); 58361 The Might of Mograine | 32839 Beam (Red) 1.8x | 990006 Pool of Blood 3x |
| Unholy | 5x: 61894 Spirit Particles (green - Base); 43167 Spirit Particles (green) | 63319 Saronite Animus Formation Visual 1x; 60426 Ghost State 1x | 191206 `SC_CastingCircle_01` 1.4x; 990009 coluna verde 10x |

- **Escala:** o tamanho de um efeito de aura vem do modelo e da escala no `SpellVisualEffectName.dbc`; pelo servidor
  só a escala da unidade muda o tamanho. Por isso cada forja tem um bunny na escala das suas auras e, para as que
  precisam de outro tamanho, um bunny por aura. O beam vermelho é mais largo que o azul no próprio modelo (os dois
  com escala 1 no DBC), então fica em 1.8 contra 2.
- **Orientação:** os bunnies olham para o poço (`Positions::Center`), já que os efeitos são desenhados relativos à
  frente da unidade.
- **Aura não dummy:** 58361 (dano, cura e vida máxima) substitui a Hysteria (49016/55213/55975), que tem o mesmo
  modelo (`DeathKnight_Hysteria.mdx`) mas causa dano periódico em % da vida (mataria o bunny) e tem som. Num bunny
  que nunca luta, os efeitos da 58361 não fazem nada.
- **GOs:** criados com `GO_FLAG_NOT_SELECTABLE` (sem destaque nem tooltip). O 990006 é uma cópia tipo 5, sem nome, do
  194479 (`tradeskill_fishschool_red`): o original é um fishing hole (tipo 25), que o cliente destaca mesmo com a
  flag. Na escala do template (0.75) a poça fica escondida sob a forja.
- **Visibilidade:** forjas e visuais são vistos de longe (`VisibilityDistanceType::Large`, 200 jardas; o mapa usa
  100).
- **Trava vista de cima:** o alcance de uso da forja atravessa o teto até o andar de cima (~23 jardas acima). Mais de
  8 jardas acima das forjas, o jogador recebe um values update só dele com `GAMEOBJECT_FLAGS | GO_FLAG_INTERACT_COND`
  (cursor de mão simples) e o servidor recusa o uso ("You are too far away."). A engrenagem cinza é impossível: o
  cliente decide pelo próprio teste de alcance; `LOCKED`/`IN_USE` não mudam nada.

## Portador

| Runa | Ao pegar (visual kit, uma vez) | Auras enquanto carrega |
|---|---|---|
| Frost | 10288, da Frost Presence (48263) | 31954 Spirit Particles + 33340 Blue Portal State |
| Blood | 10283, da Blood Presence (48266) | 33338 Red Portal State + 31951 Spirit Particles |
| Unholy | 10297, da Unholy Presence (48265) | 43161 + 43167 Spirit Particles + 33339 Green Portal State |

- **Portal States:** dummy e **visíveis** na barra de buffs, relabelados pela parte 2 do Lua (ícones de DK, "Frost
  Rune"/"Blood Rune"/"Unholy Rune"), para um DK não ficar com dois ícones de presença. As Banish States usadas antes
  (33344/33343/32567) têm `SPELL_ATTR0_HIDDEN_CLIENTSIDE` e não aparecem. As partículas mantêm o próprio ícone.
- **Canceláveis:** se o jogador tira uma aura com clique direito, `MatchRunes::Update` a recoloca.
- **Acúmulos:** o `CumulativeAura` dos portais é 0 no DBC, então o campo `Applications` do pacote de aura usa as
  charges: `SetCharges` faz o cliente mostrar o número no ícone, e o Lua calcula os totais a partir dele (`UnitBuff`).
  Os modificadores param em 5 acúmulos; o número continua contando.
- **Presenças:** não são aplicadas (têm efeito de stat e trocariam a presença de um DK). Só o `ImpactKit` delas é
  tocado (`SendPlaySpellVisualKit`), uma vez: não têm `StateKit` e trazem som.
- **Modificadores:** no `UnitScript` (`ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyPeriodicDamageAurasTick`,
  `OnHeal`); `OnDamage`/`OnHeal` também alimentam o placar.

## Domo da preparação

- **Visual:** o domo do Anti-Magic Zone. O visual da 50461 é um kit de canalização (SpellVisual 11242), que só
  aparece enquanto uma unidade "canaliza" a spell: o NPC `28306` (o totem do AMZ) fica no spawn com
  `SetChannelSpellId(50461)`, como os spirit guides fazem com o feixe deles, sem aplicar a aura. Escala 2 (o AMZ tem
  ~7 jardas na escala 1). Jogadores não veem o NPC; GMs veem um totem de ar pequeno. A 72628 "Anti-Magic Zone
  (Small)" foi descartada (escala 0.25).
- **Barreira:** octógono de 8 paredes invisíveis (GO 990005, `CollisionWallPvP01` do 180322, ~11 jardas de largura na
  escala 1, usado nas BGs), tamanho 0.8, a 10 jardas do spawn, por dentro da borda do domo. Quem bloqueia é a colisão
  do cliente.
- **Reserva:** a checagem de 1 s devolve ao spawn quem passar de 13 jardas.
- Somem quando a batalha começa (também com `.acherus begin`) e no fim.

## Barreira da escada

A escada atrás do portal leva à sacada, e as frestas dos lados levam para onde o cliente troca a zona para "The
Heart of Acherus". Quatro pontos (`Positions::StairsBarrier`) formam três segmentos, cada um com uma fileira de
paredes 990005 sobrepostas:

- todas no **piso mais baixo** da barreira (420.71), escaladas (~1.51) até 3 jardas acima do andar de cima: nada
  passa por baixo e a barra quadriculada visível da base (`GameObjectDisplayInfo`: -0.36 a -0.07) fica enterrada;
- caixa de colisão na escala 1 (`GameObjectModels.dtree`): 11.078 de largura e 17.562 de altura;
- fileiras empilhadas ou bases flutuando mostravam a barra e deixavam fresta, por isso uma fileira só.

No topo da escada, um bunny com 42049 (Boss Frost Portal State) em escala 8 marca a passagem fechada. Alternativa
considerada, não implementada: teleporte ao cruzar a linha, se alguém ainda passar.

## Portal para o andar de cima

GO 191539 (`Doodad_Nox_portal_purple_bossroom17`, o portal do Acherus junto à porta), criado na phase da partida no
início da batalha. Como o original (aura 54724 do NPC 29581, raio 3 jardas), o teleporte vem de ficar sobre ele:
checado a cada segundo (`MatchHall::UsePortal`), leva a 2517.90, -5554.81, 444.12. Fantasmas no salão também o usam.

## Livro de instruções

- GO 990007 tipo 9 (texto), modelo do Lexicon of Power (display 8520), tamanho 2.5, `Data0` página 990000, `Data2` 2
  (material Stone), nome "O Coração de Acherus"; 6 jardas à frente do spawn, 1.2 acima do piso, coluna azul 990008
  (display 298, tamanho 2) embaixo. Some com a preparação.
- Páginas 990000 → 990001 em pt-BR (regra: textos de informação em português, UI e mecânica em inglês).
- O texto da página é SimpleHTML: funcionam `<HTML><BODY>`, `<P>`, `align="center|right"`, `<BR/>`, `|cffRRGGBB` e
  acentos UTF-8. Não funcionam tamanho de H1/H2/H3 sozinho nem `<IMG>`; `|T...|t` só foi testado com caminho
  quebrado.
- A parte 2 do Lua aumenta o H1 só deste livro (`SetFontObject("h1", ...)`; `SetFont("h1", ...)` dá erro no 3.3.5) e
  desenha 3 ícones de runa sob o título da primeira página, nas linhas em branco reservadas.
- Materiais (`Data2`): 1 Parchment, 2 Stone, 3 Marble, 4 Silver, 5 Bronze, 6 Valentine, 7 Illidan (papel simples).
- O cliente guarda templates de GO e páginas em cache: reiniciar o servidor e apagar `Cache\WDB` do cliente; o Lua
  pede relog ou `/reload`.

## Pesquisa no DBC

- Os efeitos permanentes de uma aura ficam no `StateKit` (campo 4 do `SpellVisual.dbc`), nos campos de modelo do
  `SpellVisualKit.dbc` e no `SpellVisualKitModelAttach.dbc` (é por ali que a 72915 prende o `sc_spirits_02`).
- O som do kit é o campo 15 do `SpellVisualKit.dbc`.
- `Spell.dbc`: visual no campo 131, atributo 0 no campo 4 (0x80 = ícone de aura oculto).
- Auras testadas ficam no `GM.log` (`Command: aura <id>`).

**Descartados:** skybeams 62893/62894/62895 (altos demais), 72915 Arthas Teleporter Ceremony, 54717 e 75498 (neve),
52574/52679 (radiation), 180713 Light of Elune, 190564 Acherus Teleport Rune.
