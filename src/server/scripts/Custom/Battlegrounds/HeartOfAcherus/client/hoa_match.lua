-- Heart of Acherus: client UI part 2 (match), see docs/client-ui.md.
--
-- Sent to queued and in-match players, after part 1 (hoa_login.lua), whose AcherusBG_UI helpers it
-- extends. The AcherusBG_Part2 guard keeps it idempotent.

if not AcherusBG_UI then return end
if AcherusBG_Part2 then return end
AcherusBG_Part2 = true

local IsRealEyeOfTheStorm = AcherusBG_UI.IsRealEyeOfTheStorm

-- ---------------------------------------------------------------------------- rune carrier auras
-- Each carrier wears a visible dummy "Portal State" (33338 Blood, 33339 Unholy, 33340 Frost), relabeled here with
-- death knight icons. The three share one DBC icon, so they are told apart by the localized spell name.

local RUNE_ICON_BLOOD = "Interface\\Icons\\Spell_Deathknight_BladedArmor"
local RUNE_ICON_UNHOLY = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade"
local RUNE_ICON_FROST = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade2"
local RUNE_DESC = "Carrying a rune from the runeforges of Acherus."

local RUNE_AURAS = {}
do
    local defs = {
        { 33338, "Blood Rune", RUNE_ICON_BLOOD },
        { 33339, "Unholy Rune", RUNE_ICON_UNHOLY },
        { 33340, "Frost Rune", RUNE_ICON_FROST },
    }
    for _, def in ipairs(defs) do
        local spellName = GetSpellInfo(def[1])
        if spellName then
            RUNE_AURAS[spellName] = {
                name = def[2],
                icon = def[3],
                pattern = string.gsub(spellName, "%W", "%%%0"),
            }
        end
    end
end

-- count = the stacks the server mirrors as charges; filter is passed through so a debuff (HARMFUL) never reads the
-- helpful list, where the rune lives
local function RuneAuraFor(unit, index, filter)
    local name, _, _, count = UnitAura(unit, index, filter)
    return name and RUNE_AURAS[name], count or 0
end

-- player buff frame; "buttonName" is the button name prefix, "BuffButton" for buffs
hooksecurefunc("AuraButton_Update", function(buttonName, index)
    if not AcherusBG_UI.active or buttonName ~= "BuffButton" then
        return
    end
    local def = RuneAuraFor("player", index)
    if def then
        local icon = _G[buttonName .. index .. "Icon"]
        if icon then
            icon:SetTexture(def.icon)
        end
    end
end)

-- target frame, shared with the boss frames
hooksecurefunc("TargetFrame_UpdateAuras", function(self)
    if not AcherusBG_UI.active then
        return
    end
    local frameName = self:GetName()
    local maxBuffs = MAX_TARGET_BUFFS or 32
    for i = 1, maxBuffs do
        local def = RuneAuraFor(self.unit, i)
        if def then
            local icon = _G[frameName .. "Buff" .. i .. "Icon"]
            if icon then
                icon:SetTexture(def.icon)
            end
        end
    end
end)

-- raid frame (UIParent.lua RefreshAuras, called with the "Aura" suffix)
hooksecurefunc("RefreshAuras", function(frame, unit)
    if not AcherusBG_UI.active then
        return
    end
    local frameName = frame:GetName()
    local maxAuras = MAX_RAID_AURAS or 4
    for i = 1, maxAuras do
        local def = RuneAuraFor(unit, i)
        if def then
            local icon = _G[frameName .. "Aura" .. i .. "Icon"]
            if icon then
                icon:SetTexture(def.icon)
            end
        end
    end
end)

-- party buff tooltip; it only shows icons, and the index only counts the auras that are actually present
hooksecurefunc("PartyMemberBuffTooltip_Update", function(self)
    if not AcherusBG_UI.active then
        return
    end
    local index = 1
    local maxBuffs = MAX_PARTY_TOOLTIP_BUFFS or 16
    for i = 1, maxBuffs do
        local name = UnitBuff(self.unit, i)
        if name then
            local def = RUNE_AURAS[name]
            if def then
                local icon = _G["PartyMemberBuffTooltipBuff" .. index .. "Icon"]
                if icon then
                    icon:SetTexture(def.icon)
                end
            end
            index = index + 1
        end
    end
end)

-- aura tooltips (SetUnitAura for the player's buffs and debuffs, SetUnitBuff elsewhere): the description has no Lua
-- API, so it is appended with the totals of the current stacks; Show() recomputes the height. "filter" keeps a
-- debuff tooltip (HARMFUL) from being read as the helpful rune aura.
local function RuneTooltip(self, unit, index, filter)
    if not AcherusBG_UI.active then
        return
    end
    local def, count = RuneAuraFor(unit, index, filter)
    if not def then
        return
    end
    local line = _G[self:GetName() .. "TextLeft1"]
    if line then
        line:SetText(def.name)
    end

    local stacks = (count and count > 0) and count or 1
    self:AddLine(RUNE_DESC, 1, 1, 1)
    self:AddLine(string.format("Damage done: +%d%%", math.min(100, 20 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Damage taken: +%d%%", math.min(100, 20 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Healing received: -%d%%", math.min(50, 10 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Size: +%d%%", math.min(100, 20 + 20 * (stacks - 1))), 1, 1, 1)
    self:AddLine(string.format("(%d stack%s, +1 every 15 sec)", stacks, stacks == 1 and "" or "s"), 1, 1, 1)
    self:Show()
end

hooksecurefunc(GameTooltip, "SetUnitAura", RuneTooltip)
hooksecurefunc(GameTooltip, "SetUnitBuff", RuneTooltip)

-- floating combat text: the aura name is baked into the message, so rewrite it in the funnel
if CombatText_AddMessage then
    local BaseCombatText = CombatText_AddMessage
    CombatText_AddMessage = function(message, ...)
        if AcherusBG_UI.active and type(message) == "string" then
            for _, def in pairs(RUNE_AURAS) do
                if string.find(message, def.pattern) then
                    message = string.gsub(message, def.pattern, def.name)
                end
            end
        end
        return BaseCombatText(message, ...)
    end
end

-- inactive -> active: re-render the aura frames once. After a /reload they were drawn with the portal icon, and a
-- permanent aura never triggers the next update our hooks wait for
local runesWereActive = false

local function RefreshRuneAuras()
    if BuffFrame_Update then
        pcall(BuffFrame_Update)
    end
    if TargetFrame_UpdateAuras then
        if TargetFrame then
            pcall(TargetFrame_UpdateAuras, TargetFrame)
        end
        for i = 1, (MAX_BOSS_FRAMES or 4) do
            local boss = _G["Boss" .. i .. "TargetFrame"]
            if boss and boss:IsShown() then
                pcall(TargetFrame_UpdateAuras, boss)
            end
        end
    end
end

local function OnActiveChanged()
    local active = AcherusBG_UI.active and not IsRealEyeOfTheStorm()
    if active and not runesWereActive then
        RefreshRuneAuras()
    end
    runesWereActive = active
end

-- the server flips AcherusBG_UI.active and calls Relabel(): wrap it to catch the transition
local RelabelBase = AcherusBG_UI.Relabel
if RelabelBase then
    AcherusBG_UI.Relabel = function()
        OnActiveChanged()
        local result = RelabelBase()
        -- the battlefield status can arrive before the active flag flips, leaving the list queue icon on the
        -- Eye of the Storm row: re-place it now instead of waiting for the next list update
        if AcherusBG_UI.AcherusQueueStatus then
            AcherusBG_UI.AcherusQueueStatus()
        end
        return result
    end
end

-- ---------------------------------------------------------------------------- minimap rune markers
-- The server calls AcherusBG_Runes.Update(px, py, x, y, name, ...) every 0.25 s with world coordinates (yards).
-- On the minimap the Ebon Hold is rotated 90 degrees and mirrored: a world offset (dx, dy) is (-dy, dx) there.

local RUNE_PRESENCE_ICON = {
    "Interface\\Icons\\Spell_Deathknight_FrostPresence",
    "Interface\\Icons\\Spell_Deathknight_BloodPresence",
    "Interface\\Icons\\Spell_Deathknight_UnholyPresence",
}

-- yards covered across the minimap at each zoom level (0 based like Minimap:GetZoom). Astrolabe's indoor
-- MinimapSize is 20% too large for the Ebon Hold, so these values are already scaled down to match.
local MINIMAP_WORLD_SPAN = { [0] = 250, [1] = 200, [2] = 150, [3] = 100, [4] = 66 + 2 / 3, [5] = 41 + 2 / 3 }

-- map <-> world transform of the Ebon Hold, from the client's WorldMapArea.dbc (same for every client of
-- this build): worldY = MAP_A + MAP_B * mapX, worldX = MAP_C + MAP_D * mapY
local MAP_A, MAP_B = -4050, -3159
local MAP_C, MAP_D = 3087, -2108

AcherusBG_Runes = { data = nil, icons = {} }

local function CreateMarker(texture)
    local frame = CreateFrame("Frame", nil, Minimap)
    frame:SetSize(18, 18)
    -- one level above the map terrain, below every minimap button: the Blizzard buttons sit at
    -- MinimapBackdrop+1/+2 (Minimap+2/+3) and MiniMapInstanceDifficulty at MinimapCluster+10
    frame:SetFrameLevel(Minimap:GetFrameLevel() + 1)

    frame.icon = frame:CreateTexture(nil, "ARTWORK")
    frame.icon:SetAllPoints()
    frame.icon:SetTexture(texture)
    -- spell icons carry a light border baked into the texture; crop it off on whole texels of the 64x64
    -- icon (4/64 and 60/64), so the downscale does not blend a fractional border row into the edge
    frame.icon:SetTexCoord(0.0625, 0.9375, 0.0625, 0.9375)

    frame:Hide()
    return frame
end

for i = 1, 3 do
    AcherusBG_Runes.icons[i] = CreateMarker(RUNE_PRESENCE_ICON[i])
end

local function HideRuneMarkers()
    for i = 1, 3 do
        AcherusBG_Runes.icons[i]:Hide()
    end
end

-- a same team rune carrier is a battlefield raid member: find its unit token by name so the marker can
-- follow GetPlayerMapPosition (smooth) instead of the 0.25 s server updates
local function FindUnitByName(name)
    if not name or name == "" then
        return nil
    end
    if UnitName("player") == name then
        return "player"
    end
    for i = 1, 40 do
        local unit = "raid" .. i
        if UnitName(unit) == name then
            return unit
        end
    end
    for i = 1, 4 do
        local unit = "party" .. i
        if UnitName(unit) == name then
            return unit
        end
    end
    return nil
end

local markerDriver = CreateFrame("Frame")
markerDriver:SetScript("OnUpdate", function()
    local data = AcherusBG_Runes.data
    if not data or not AcherusBG_UI.active or (GetTime() - data.time) > 2 then
        HideRuneMarkers()
        return
    end

    -- the player position is known every frame through GetPlayerMapPosition, converted back to world with
    -- the fixed transform so the anchor moves smoothly (falls back to the server position while invalid)
    local playerX, playerY = data.px, data.py
    local mapX, mapY = GetPlayerMapPosition("player")
    if mapX and not (mapX == 0 and mapY == 0) then
        playerY = MAP_A + MAP_B * mapX
        playerX = MAP_C + MAP_D * mapY
    end

    local zoom = Minimap:GetZoom() or 0
    local pixelsPerYard = Minimap:GetWidth() / (MINIMAP_WORLD_SPAN[zoom] or 250)

    -- rotating minimap: the client spins the map by the player's facing, but its math runs with the map
    -- origin at the top-left (y down) while our offsets are screen space (y up), so the sign is flipped
    local rotate = GetCVar("rotateMinimap") ~= "0"
    local facing = rotate and -(GetPlayerFacing() or 0) or 0
    local sinFacing, cosFacing = math.sin(facing), math.cos(facing)

    -- clamp the markers to the minimap edge (icon half size plus a small margin), so far ones stay on the rim
    local margin = 11
    local halfW = Minimap:GetWidth() / 2 - margin
    local halfH = Minimap:GetHeight() / 2 - margin
    local isSquare = GetMinimapShape and GetMinimapShape() == "SQUARE"

    local function Place(icon, worldX, worldY)
        -- the Ebon Hold world map is rotated 90 degrees and mirrored on the minimap:
        -- minimap X from the world Y, minimap Y from the world X (inverted)
        local dx = -(worldY - playerY) * pixelsPerYard
        local dy = (worldX - playerX) * pixelsPerYard

        if rotate then
            dx, dy = dx * cosFacing - dy * sinFacing, dx * sinFacing + dy * cosFacing
        end

        local dist = isSquare and math.max(math.abs(dx), math.abs(dy)) or math.sqrt(dx * dx + dy * dy)
        local maxDist = isSquare and math.min(halfW, halfH) or halfW
        if dist > maxDist then
            local factor = maxDist / dist
            dx, dy = dx * factor, dy * factor
        end

        -- re-anchor only once the marker moved at least a pixel: re-rasterizing the downscaled icon at
        -- sub-pixel offsets every frame is what makes it shimmer while moving
        if icon:IsShown() and icon.lastX and math.abs(dx - icon.lastX) < 1 and math.abs(dy - icon.lastY) < 1 then
            return
        end
        icon.lastX, icon.lastY = dx, dy

        icon:ClearAllPoints()
        icon:SetPoint("CENTER", Minimap, "CENTER", dx, dy)
        icon:Show()
    end

    for i = 1, 3 do
        local rune = data.runes[i]
        local icon = AcherusBG_Runes.icons[i]
        if rune then
            -- a same team carrier follows its battlefield raid unit for a smooth position; the others
            -- (enemy carriers and the forges) use the last server position
            local runeX, runeY = rune.x, rune.y
            if rune.unit then
                local ux, uy = GetPlayerMapPosition(rune.unit)
                if ux and not (ux == 0 and uy == 0) then
                    runeY = MAP_A + MAP_B * ux
                    runeX = MAP_C + MAP_D * uy
                end
            end

            Place(icon, runeX, runeY)
        else
            icon:Hide()
        end
    end
end)
markerDriver:Show()

-- Called by the server every 0.25 s with the player position (the anchor) and each rune's position, plus
-- (for a carrier of the observer's team) its name so the client can follow that unit smoothly.
function AcherusBG_Runes.Update(px, py, fx, fy, fName, bx, by, bName, ux, uy, uName)
    local function Store(x, y, name)
        return { x = x, y = y, unit = FindUnitByName(name) }
    end

    AcherusBG_Runes.data = {
        time = GetTime(),
        px = px,
        py = py,
        runes = {
            Store(fx, fy, fName),
            Store(bx, by, bName),
            Store(ux, uy, uName),
        },
    }
end

-- ---------------------------------------------------------------------------- outdoor spells methods
-- The client refuses outdoor-only spells (mounts, Travel Form, Ghost Wolf) indoors by itself, nothing reaches the
-- server. The server puts the active HeartOfAcherus.OutdoorSpellsMethod in AcherusBG_OutdoorSpellsMethod at the top
-- of this payload; each method checks its id.

local OUTDOOR_METHOD_MOUNT_BUTTON = 1
local OUTDOOR_METHOD_CLIENT_HOOK = 2

-- the server fakes an Acherus match as an active battlefield
local function IsInAcherusMatch()
    if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
        return false
    end
    for i = 1, (MAX_BATTLEFIELD_QUEUES or 2) do
        if GetBattlefieldStatus(i) == "active" then
            return true
        end
    end
    return false
end

-- ---------------------------------------------------------------------------- method 1: mount button
-- In a match this button asks the server to cast the Acherus Deathcharger (or to dismount). Bindable with
-- /click AcherusBGMountButton. Draggable; the position goes to a client CVar (no SavedVariables here, and the
-- layout cache skips frames created after the login).

local MOUNT_SPELL = 48778
local MOUNT_BUTTON_SIZE = 56
local MOUNT_BUTTON_Y = 220 -- default, from the bottom of the screen, above the action bars
local MOUNT_POSITION_CVAR = "acherusMountButton"

local mountButton = CreateFrame("Button", "AcherusBGMountButton", UIParent)
mountButton:SetWidth(MOUNT_BUTTON_SIZE)
mountButton:SetHeight(MOUNT_BUTTON_SIZE)
mountButton:SetClampedToScreen(true)
mountButton:SetMovable(true)
mountButton:RegisterForDrag("LeftButton")
mountButton:Hide()

-- registering an existing CVar (after a /reload) may fail, reading and writing are guarded on their own
if RegisterCVar then
    pcall(RegisterCVar, MOUNT_POSITION_CVAR, "")
end

-- the saved position is "left,bottom" in UIParent coordinates
local function PlaceMountButton()
    mountButton:ClearAllPoints()
    local ok, saved = pcall(GetCVar, MOUNT_POSITION_CVAR)
    local left, bottom = string.match(ok and saved or "", "^(-?[%d%.]+),(-?[%d%.]+)$")
    if left and bottom then
        mountButton:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", tonumber(left), tonumber(bottom))
    else
        mountButton:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, MOUNT_BUTTON_Y)
    end
end

PlaceMountButton()

mountButton:SetScript("OnDragStart", function(self)
    self:StartMoving()
end)

mountButton:SetScript("OnDragStop", function(self)
    self:StopMovingOrSizing()
    self:SetUserPlaced(false)
    local left, bottom = self:GetLeft(), self:GetBottom()
    if left and bottom then
        pcall(SetCVar, MOUNT_POSITION_CVAR, string.format("%.1f,%.1f", left, bottom))
        self:ClearAllPoints()
        self:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", left, bottom)
    end
end)

local mountIcon = mountButton:CreateTexture(nil, "BACKGROUND")
mountIcon:SetAllPoints(mountButton)
mountIcon:SetTexture(select(3, GetSpellInfo(MOUNT_SPELL)))

mountButton:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress")
mountButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")

local mountLabel = mountButton:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
mountLabel:SetPoint("TOP", mountButton, "BOTTOM", 0, -2)
mountLabel:SetText(GetSpellInfo(MOUNT_SPELL) or "Acherus Deathcharger")

mountButton:SetScript("OnClick", function()
    -- a whisper to self reaches the server for every player (the GUILD channel does not send without a guild)
    SendAddonMessage('AcherusBG', IsMounted() and 'dismount' or 'mount', 'WHISPER', UnitName('player'))
end)

mountButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText(GetSpellInfo(MOUNT_SPELL) or "Acherus Deathcharger", 1, 1, 1)
    GameTooltip:AddLine("Summons or dismisses your Acherus Deathcharger. This is a very fast mount.", nil, nil, nil, true)
    GameTooltip:AddLine("Usable anywhere in the Heart of Acherus.", 0, 1, 0, true)
    GameTooltip:Show()
end)

mountButton:SetScript("OnLeave", function()
    GameTooltip:Hide()
end)

-- the rune carrier aura (see RUNE_AURAS), death, combat and the forms that forbid mounts (druid forms, Ghost Wolf)
-- grey the icon out like an unusable action; it stays clickable and the server tells why. Mounted, it dismounts.
local function CanMount()
    if UnitIsDeadOrGhost("player") or UnitAffectingCombat("player") then
        return false
    end

    local _, class = UnitClass("player")
    if (class == "DRUID" or class == "SHAMAN") and GetShapeshiftForm() ~= 0 then
        return false
    end

    for spellName in pairs(RUNE_AURAS) do
        if UnitBuff("player", spellName) then
            return false
        end
    end
    return true
end

local function UpdateMountUsable()
    local usable = IsMounted() or CanMount()
    mountIcon:SetDesaturated(not usable)
    if usable then
        mountIcon:SetVertexColor(1, 1, 1)
    else
        mountIcon:SetVertexColor(0.4, 0.4, 0.4)
    end
end

-- shown while the player is in an Acherus match
local function UpdateMountButton()
    if AcherusBG_OutdoorSpellsMethod == OUTDOOR_METHOD_MOUNT_BUTTON and IsInAcherusMatch() then
        UpdateMountUsable()
        mountButton:Show()
    else
        mountButton:Hide()
    end
end

local mountWatcher = CreateFrame("Frame")
mountWatcher:RegisterEvent("UPDATE_BATTLEFIELD_STATUS")
mountWatcher:RegisterEvent("PLAYER_ENTERING_WORLD")
mountWatcher:SetScript("OnEvent", UpdateMountButton)

local mountUsableWatcher = CreateFrame("Frame")
mountUsableWatcher:RegisterEvent("PLAYER_REGEN_DISABLED")
mountUsableWatcher:RegisterEvent("PLAYER_REGEN_ENABLED")
mountUsableWatcher:RegisterEvent("PLAYER_DEAD")
mountUsableWatcher:RegisterEvent("PLAYER_ALIVE")
mountUsableWatcher:RegisterEvent("PLAYER_UNGHOST")
mountUsableWatcher:RegisterEvent("UPDATE_SHAPESHIFT_FORM")
mountUsableWatcher:RegisterEvent("UNIT_AURA")
mountUsableWatcher:SetScript("OnEvent", function(self, event, unit)
    if event == "UNIT_AURA" and unit ~= "player" then
        return
    end
    if mountButton:IsShown() then
        UpdateMountUsable()
    end
end)

-- the server flips AcherusBG_UI.active and calls Relabel() right after
local RelabelBeforeMount = AcherusBG_UI.Relabel
if RelabelBeforeMount then
    AcherusBG_UI.Relabel = function()
        local result = RelabelBeforeMount()
        UpdateMountButton()
        return result
    end
end

-- this part may arrive with the match already running, after the events above
UpdateMountButton()

-- ---------------------------------------------------------------------------- client hook (methods 1 and 2)
-- The casting functions are post-hooked (hooksecurefunc, so the action bars stay untainted) to remember the spell
-- the player just tried. Forms (methods 1 and 2): when the client's own outdoors refusal comes with it, the error is
-- swallowed and the server is asked to cast the form; a cast the client lets through is never asked for, so it is
-- never doubled. Mounts (method 2): the click itself asks the server, because for a while after a dismount the client
-- ignores a mount click without any error; "mount <spell id>" or "dismount", so a repeated click never toggles twice.
-- The error may come before or after the hook, so both wait HOOK_WINDOW for each other; an unpaired error is shown late.

local HOOK_WINDOW = 0.3
local FORM_SPELLS = { 783, 2645 } -- Travel Form, Ghost Wolf

local formSpells = {}
for _, spellId in ipairs(FORM_SPELLS) do
    local spellName = GetSpellInfo(spellId)
    if spellName then
        formSpells[spellName] = spellId
    end
end

local function HookActive()
    return (AcherusBG_OutdoorSpellsMethod == OUTDOOR_METHOD_MOUNT_BUTTON
        or AcherusBG_OutdoorSpellsMethod == OUTDOOR_METHOD_CLIENT_HOOK) and IsInAcherusMatch()
end

-- IsMounted() as of the last frame: a click on the active mount dismounts on the client before the hooks run, which
-- must not read it as a click to mount
local mountedLastFrame = false
local mountedWatcher = CreateFrame("Frame")
mountedWatcher:SetScript("OnUpdate", function()
    mountedLastFrame = IsMounted()
end)

-- mounted, any mount click dismounts (the server ignores it if the client already did); in combat the client's own
-- refusal stays
local function MountRequest(spellId)
    if AcherusBG_OutdoorSpellsMethod ~= OUTDOOR_METHOD_CLIENT_HOOK or not spellId then
        return nil
    end
    if IsMounted() or mountedLastFrame then
        return "dismount"
    end
    if UnitAffectingCombat("player") then
        return nil
    end
    return "mount " .. spellId
end

local function IsMountRequest(body)
    return body == "dismount" or string.find(body, "^mount ") ~= nil
end

local function MountSpellByName(spellName)
    for i = 1, GetNumCompanions("MOUNT") do
        local _, _, spellId = GetCompanionInfo("MOUNT", i)
        if spellId and GetSpellInfo(spellId) == spellName then
            return spellId
        end
    end
end

-- an action bar companion: its id is the spell or the creature, or else the index in the mount list
local function MountSpellByAction(id)
    local count = GetNumCompanions("MOUNT")
    for i = 1, count do
        local creatureId, _, spellId = GetCompanionInfo("MOUNT", i)
        if id == spellId or id == creatureId then
            return spellId
        end
    end
    if id and id >= 1 and id <= count then
        return (select(3, GetCompanionInfo("MOUNT", id)))
    end
end

-- "!Travel Form" and "Travel Form(Rank 1)" as given to /cast. "!" keeps an active form instead of leaving it: the
-- form is cast again, which druids use to drop slows, so the server gets "form! <id>" instead of "form <id>"
local function SpellRequest(spellName)
    if type(spellName) ~= "string" then
        return nil
    end
    local keep = string.find(spellName, "^%s*!") ~= nil
    spellName = string.gsub(spellName, "^%s*!", "")
    spellName = string.gsub(spellName, "%s*%(.-%)%s*$", "")
    if formSpells[spellName] then
        return (keep and "form! " or "form ") .. formSpells[spellName]
    end
    return MountRequest(MountSpellByName(spellName))
end

local attempt, attemptTime -- request for the spell the player just tried
local refusal, refusalTime -- the refusal text, shown late if nothing pairs with it
local refusalTimer = CreateFrame("Frame")
refusalTimer:Hide()

local lastMountRequest = 0

local function SendRequest(body)
    -- one click may reach more than one hook
    if IsMountRequest(body) then
        if GetTime() - lastMountRequest < HOOK_WINDOW then
            return
        end
        lastMountRequest = GetTime()
    end
    SendAddonMessage('AcherusBG', body, 'WHISPER', UnitName('player'))
end

-- "You are in combat" for a click while mounted only pairs with a mount request, which was sent with the click:
-- pairing just swallows it
local function TryPair()
    if not (attempt and refusal and math.abs(attemptTime - refusalTime) <= HOOK_WINDOW) then
        return
    end
    if refusal == SPELL_FAILED_AFFECTING_COMBAT and not IsMountRequest(attempt) then
        return
    end
    local body = attempt
    attempt, refusal = nil, nil
    refusalTimer:Hide()
    if not IsMountRequest(body) then
        SendRequest(body)
    end
end

local function OnAttempt(body)
    if body and HookActive() then
        attempt, attemptTime = body, GetTime()
        if IsMountRequest(body) then
            SendRequest(body)
        end
        TryPair()
    end
end

-- UI addons with their own error filter (KkthnxUI) show UI_ERROR_MESSAGE through their own frame, past the OnEvent
-- below, so in a match the hook's refusals are also dropped where every path ends: UIErrorsFrame:AddMessage. The
-- late display of an unpaired refusal goes through
local HIDDEN_REFUSALS = {}
for _, message in ipairs({ SPELL_FAILED_NO_MOUNTS_ALLOWED, SPELL_FAILED_ONLY_OUTDOORS }) do
    HIDDEN_REFUSALS[message] = true
end

local showingRefusal = false
local BaseAddMessage = UIErrorsFrame.AddMessage
UIErrorsFrame.AddMessage = function(self, message, ...)
    if not showingRefusal and message and HIDDEN_REFUSALS[message] and HookActive() then
        return
    end
    return BaseAddMessage(self, message, ...)
end

refusalTimer:SetScript("OnUpdate", function(self)
    if refusal and GetTime() - refusalTime > HOOK_WINDOW then
        showingRefusal = true
        UIErrorsFrame:AddMessage(refusal, 1.0, 0.1, 0.1, 1.0)
        showingRefusal = false
        refusal = nil
    end
    if not refusal then
        self:Hide()
    end
end)

local function ActionRequest(slot)
    local actionType, id, subType, spellId = GetActionInfo(slot)
    if actionType == "spell" then
        if spellId then
            return SpellRequest(GetSpellInfo(spellId))
        elseif subType ~= "pet" then
            return SpellRequest(GetSpellName(id, "spell"))
        end
    elseif actionType == "companion" and subType == "MOUNT" then
        return MountRequest(MountSpellByAction(id))
    elseif actionType == "macro" then
        return SpellRequest(GetMacroSpell(id))
    end
end

-- a macro casts through CastSpellByName, hooked below with its "!"; this hook runs after it and would replace it
hooksecurefunc("UseAction", function(slot)
    if GetActionInfo(slot) ~= "macro" then
        OnAttempt(ActionRequest(slot))
    end
end)

hooksecurefunc("CastSpellByName", function(spellName)
    OnAttempt(SpellRequest(spellName))
end)

hooksecurefunc("CastSpell", function(index, book)
    if book ~= "pet" then
        OnAttempt(SpellRequest(GetSpellName(index, book)))
    end
end)

hooksecurefunc("CastShapeshiftForm", function(index)
    OnAttempt(SpellRequest(select(2, GetShapeshiftFormInfo(index))))
end)

hooksecurefunc("CallCompanion", function(companionType, index)
    if companionType == "MOUNT" then
        OnAttempt(MountRequest((select(3, GetCompanionInfo("MOUNT", index)))))
    end
end)

local ErrorsOnEvent = UIErrorsFrame:GetScript("OnEvent")
UIErrorsFrame:SetScript("OnEvent", function(self, event, message, ...)
    if event == "UI_ERROR_MESSAGE" and HookActive() then
        -- the hall counts as outdoors for the server: never shown in a match, a mount goes through the hook or the button
        if message == SPELL_FAILED_NO_MOUNTS_ALLOWED then
            return
        end
        if message == SPELL_FAILED_ONLY_OUTDOORS
            or (message == SPELL_FAILED_AFFECTING_COMBAT and IsMounted()
                and AcherusBG_OutdoorSpellsMethod == OUTDOOR_METHOD_CLIENT_HOOK) then
            refusal, refusalTime = message, GetTime()
            refusalTimer:Show()
            TryPair()
            return
        end
    end
    return ErrorsOnEvent(self, event, message, ...)
end)

-- The client also draws these spells unusable in the hall (grey icon). In a match the hook makes them usable, so the
-- Blizzard action and shapeshift buttons get the usable color back after their own update, unless mana is short.

local function IsCarryingRune()
    for spellName in pairs(RUNE_AURAS) do
        if UnitBuff("player", spellName) then
            return true
        end
    end
    return false
end

local ACTION_BUTTON_PREFIXES = { "ActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton",
    "MultiBarRightButton", "MultiBarLeftButton", "BonusActionButton" }

if ActionButton_UpdateUsable then
    hooksecurefunc("ActionButton_UpdateUsable", function(self)
        local request = self.action and HookActive() and ActionRequest(self.action)
        if not request then
            return
        end
        -- mounts stay grey in combat, like anywhere else, even mounted (the click still dismounts), and while carrying a
        -- rune (the click shows why)
        if IsMountRequest(request) and (UnitAffectingCombat("player") or IsCarryingRune()) then
            return
        end
        local isUsable, notEnoughMana = IsUsableAction(self.action)
        local icon = _G[self:GetName() .. "Icon"]
        if icon and not isUsable and not notEnoughMana then
            icon:SetVertexColor(1.0, 1.0, 1.0)
        end
    end)
end

if ShapeshiftBar_UpdateState then
    hooksecurefunc("ShapeshiftBar_UpdateState", function()
        if not HookActive() then
            return
        end
        for i = 1, GetNumShapeshiftForms() do
            local _, spellName, _, isCastable = GetShapeshiftFormInfo(i)
            local icon = _G["ShapeshiftButton" .. i .. "Icon"]
            if icon and not isCastable and SpellRequest(spellName) and not select(2, IsUsableSpell(spellName)) then
                icon:SetVertexColor(1.0, 1.0, 1.0)
            end
        end
    end)
end

-- the buttons update on their own events, not when a match starts or ends, nor on combat or mount changes
local function RefreshUsableLook()
    if ActionButton_UpdateUsable then
        for _, prefix in ipairs(ACTION_BUTTON_PREFIXES) do
            for i = 1, (NUM_ACTIONBAR_BUTTONS or 12) do
                local button = _G[prefix .. i]
                if button and button.action then
                    ActionButton_UpdateUsable(button)
                end
            end
        end
    end
    if ShapeshiftBar_UpdateState then
        ShapeshiftBar_UpdateState()
    end
end

local usableLookWatcher = CreateFrame("Frame")
usableLookWatcher:RegisterEvent("UPDATE_BATTLEFIELD_STATUS")
usableLookWatcher:RegisterEvent("PLAYER_ENTERING_WORLD")
usableLookWatcher:RegisterEvent("PLAYER_REGEN_DISABLED")
usableLookWatcher:RegisterEvent("PLAYER_REGEN_ENABLED")
usableLookWatcher:RegisterEvent("COMPANION_UPDATE")
usableLookWatcher:RegisterEvent("UNIT_AURA")
usableLookWatcher:SetScript("OnEvent", function(self, event, unit)
    if event == "UNIT_AURA" and unit ~= "player" then
        return
    end
    if HookActive() or event == "UPDATE_BATTLEFIELD_STATUS" or event == "PLAYER_ENTERING_WORLD" then
        RefreshUsableLook()
    end
end)

local RelabelBeforeUsableLook = AcherusBG_UI.Relabel
if RelabelBeforeUsableLook then
    AcherusBG_UI.Relabel = function()
        local result = RelabelBeforeUsableLook()
        RefreshUsableLook()
        return result
    end
end

RefreshUsableLook()

-- ---------------------------------------------------------------------------- instruction book
-- The starting area book is a page text (ItemTextFrame, SimpleHTML), which has no H1 font and draws no icons.
-- Our book gets a bigger H1 and a row of rune icons under the first title (the page leaves blank lines for it);
-- any other book gets the page font back for H1.

-- the gameobject name (O Coracao de Acherus with its accents) in UTF-8 bytes, so this file stays ASCII
local BOOK_NAME = "O Cora\195\167\195\163o de Acherus"
local BOOK_TITLE_SCALE = 1.6
-- same order as the rune names in the page: Blood, Frost, Unholy
local BOOK_RUNE_TEXTURES = {
    "Interface\\PlayerFrame\\UI-PlayerFrame-Deathknight-Blood",
    "Interface\\PlayerFrame\\UI-PlayerFrame-Deathknight-Frost",
    "Interface\\PlayerFrame\\UI-PlayerFrame-Deathknight-Unholy",
}
-- in page font lines: icon size and gap from the title to the top of the icons
local BOOK_RUNE_ICON_LINES = 1.8
local BOOK_RUNE_ICON_GAP = 1.0
-- distance between the icon centers, in icon sizes
local BOOK_RUNE_ICON_SPACING = 1.6

local bookRunes

-- the page font: the SimpleHTML may not report it, so fall back to the ItemTextFontNormal font object, then Morpheus 15
local function BookPageFont()
    local font, size, flags
    if ItemTextPageText.GetFont then
        local ok
        ok, font, size, flags = pcall(ItemTextPageText.GetFont, ItemTextPageText)
        if not ok then
            font = nil
        end
    end
    if (not font or not size) and ItemTextFontNormal then
        font, size, flags = ItemTextFontNormal:GetFont()
    end
    if not font or not size then
        font, size, flags = "Fonts\\MORPHEUS.TTF", 15, ""
    end
    return font, size, flags or ""
end

local function IsAcherusBook()
    return ItemTextGetItem and ItemTextGetItem() == BOOK_NAME
end

local bookTitleFont
local bookPlainFont

-- set on ITEM_TEXT_BEGIN, before ItemTextFrame lays out the page on ITEM_TEXT_READY; tries the SimpleHTML element
-- font first, then a font object for the element
local function SetBookTitleFont()
    local font, size, flags = BookPageFont()
    local titleSize = IsAcherusBook() and size * BOOK_TITLE_SCALE or size
    if pcall(ItemTextPageText.SetFont, ItemTextPageText, "h1", font, titleSize, flags) then
        return
    end

    if not bookTitleFont then
        bookTitleFont = CreateFont("AcherusBookTitleFont")
        bookPlainFont = CreateFont("AcherusBookPlainFont")
    end
    bookTitleFont:SetFont(font, size * BOOK_TITLE_SCALE, flags)
    bookPlainFont:SetFont(font, size, flags)
    ItemTextPageText:SetFontObject("h1", IsAcherusBook() and bookTitleFont or bookPlainFont)
end

local function BookRunes()
    if not bookRunes then
        bookRunes = CreateFrame("Frame", nil, ItemTextPageText)
        bookRunes:SetAllPoints(ItemTextPageText)
        bookRunes:SetFrameLevel(ItemTextPageText:GetFrameLevel() + 2)
        bookRunes.icons = {}
        for i, texture in ipairs(BOOK_RUNE_TEXTURES) do
            local icon = bookRunes:CreateTexture(nil, "OVERLAY")
            icon:SetTexture(texture)
            bookRunes.icons[i] = icon
        end
    end
    return bookRunes
end

-- the row is centered on the page, right under the title
local function ShowBookRunes()
    local _, size = BookPageFont()
    local frame = BookRunes()
    local iconSize = size * BOOK_RUNE_ICON_LINES
    local spacing = iconSize * BOOK_RUNE_ICON_SPACING
    local top = size * BOOK_TITLE_SCALE + size * BOOK_RUNE_ICON_GAP
    local first = ItemTextPageText:GetWidth() / 2 - spacing * (#frame.icons - 1) / 2
    for i, icon in ipairs(frame.icons) do
        icon:ClearAllPoints()
        icon:SetWidth(iconSize)
        icon:SetHeight(iconSize)
        icon:SetPoint("TOP", frame, "TOPLEFT", first + spacing * (i - 1), -top)
    end
    frame:Show()
end

local function UpdateBook(event)
    if event == "ITEM_TEXT_BEGIN" then
        SetBookTitleFont()
    end

    if event == "ITEM_TEXT_READY" and IsAcherusBook() and ItemTextGetPage() == 1 then
        ShowBookRunes()
    elseif bookRunes then
        bookRunes:Hide()
    end
end

if ItemTextPageText then
    local bookWatcher = CreateFrame("Frame")
    bookWatcher:RegisterEvent("ITEM_TEXT_BEGIN")
    bookWatcher:RegisterEvent("ITEM_TEXT_READY")
    bookWatcher:RegisterEvent("ITEM_TEXT_CLOSED")
    bookWatcher:SetScript("OnEvent", function(self, event)
        pcall(UpdateBook, event)
    end)
end

-- ack: both parts applied (whisper to self works without a guild; the body is valid Lua if echoed back)
SendAddonMessage('AcherusBG', 'return 3', 'WHISPER', UnitName('player'))
