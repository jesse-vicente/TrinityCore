-- Battle for Acherus: client-side UI relabels.
--
-- Pushed by the worldserver through Warden (bootstrap listener) + addon messages, see the module README.
-- Runs in the client global environment via loadstring. Keep it idempotent: the AcherusBG_UI guard prevents a
-- second execution from stacking hooks when the payload is delivered again (for example after a version
-- update or an extra handshake).

if AcherusBG_UI then return end
AcherusBG_UI = { active = false }

local TITLE = "Battle for Acherus"

-- The battleground name is localized (BattlemasterList.dbc), so we never hardcode "Eye of the Storm": we ask
-- the client for it through the same API the UI uses. The server fakes the Acherus match as the Eye of the
-- Storm, so GetBattlefieldStatus returns the very name the minimap, the dropdown and the list display.
local function LocalizedAcherusName()
    local fallback
    for i = 1, (MAX_BATTLEFIELD_QUEUES or 2) do
        local status, mapName = GetBattlefieldStatus(i)
        if status and status ~= "none" and mapName and mapName ~= "" then
            if status == "active" then
                return mapName
            end
            fallback = fallback or mapName
        end
    end
    return fallback
end

-- The server only turns the relabel on while the player is in the Acherus queue or match (AcherusBG_UI.active).
-- Belt-and-suspenders: never touch the real Eye of the Storm instance, even if a toggle was missed. The name
-- comparison uses the localized name above, so it holds on every client locale.
local function IsRealEyeOfTheStorm()
    local _, instanceType = IsInInstance()
    if instanceType ~= "pvp" then
        return false
    end
    local name = LocalizedAcherusName()
    return name ~= nil and GetRealZoneText() == name
end

-- The scoreboard stat column comes from GetBattlefieldStatInfo (localized text, tooltip and the Eye of the
-- Storm flag icon). Swap it for our own column while the relabel is active: the empty icon makes the client
-- draw the plain number (no flag, no "x") and the header uses our label and tooltip.
local ORB_POINTS_TOOLTIP = "Points earned by holding orbs and killing enemies."

if GetBattlefieldStatInfo then
    local OrigBattlefieldStatInfo = GetBattlefieldStatInfo
    GetBattlefieldStatInfo = function(index)
        if AcherusBG_UI.active and not IsRealEyeOfTheStorm() and index == 1 then
            return "Points", "", ORB_POINTS_TOOLTIP
        end
        return OrigBattlefieldStatInfo(index)
    end
end

-- Replaces an exact substring in every FontString region of a frame and its direct children.
local function Relabel(frame, from, to)
    if not frame or not frame.GetRegions then
        return
    end

    local function Fix(region)
        if region and region.GetText and region.SetText then
            local text = region:GetText()
            if type(text) == "string" and string.find(text, from, 1, true) then
                region:SetText((string.gsub(text, from, to)))
            end
        end
    end

    local regions = { frame:GetRegions() }
    for i = 1, #regions do
        Fix(regions[i])
    end

    if frame.GetNumChildren then
        for i = 1, frame:GetNumChildren() do
            local child = select(i, frame:GetChildren())
            if child and child.GetRegions then
                local childRegions = { child:GetRegions() }
                for j = 1, #childRegions do
                    Fix(childRegions[j])
                end
            end
        end
    end
end

-- Minimap battleground button tooltip: the name comes from BattlemasterList.dbc ("Eye of the Storm").
-- Hooked lazily because the button may not exist when the payload runs; RelabelAll re-checks on events.
-- The XML OnEnter installs the OnUpdate by resolving the global MiniMapBattlefieldFrame_OnUpdate, so
-- hooksecurefunc on that global makes every hover install our wrapper (original + tooltip fix). The
-- OnEnter/OnLeave SetScript churn cannot wipe it. Guard is AcherusBG_Hooked so a client that already ran a
-- previous payload re-hooks.
local function HookMinimap()
    if not MiniMapBattlefieldFrame or MiniMapBattlefieldFrame.AcherusBG_Hooked then
        return
    end
    MiniMapBattlefieldFrame.AcherusBG_Hooked = true

    local function FixTooltip()
        if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
            return
        end

        if not GameTooltip or not GameTooltip:IsShown() then
            return
        end

        local from = LocalizedAcherusName()
        if not from then
            return
        end

        for i = 1, GameTooltip:NumLines() do
            local line = _G["GameTooltipTextLeft" .. i]
            if line and line.GetText and line.SetText then
                local text = line:GetText()
                if type(text) == "string" and string.find(text, from, 1, true) then
                    line:SetText((string.gsub(text, from, TITLE)))
                end
            end
        end
    end

    if MiniMapBattlefieldFrame_OnUpdate then
        hooksecurefunc("MiniMapBattlefieldFrame_OnUpdate", FixTooltip)
    end

    -- fix the first frame of the hover too, before the OnUpdate runs
    if MiniMapBattlefieldFrame.HookScript then
        MiniMapBattlefieldFrame:HookScript("OnEnter", FixTooltip)
    end

    -- Right-click opens the dropdown menu (MiniMapBattlefieldDropDown_Initialize) whose title is the
    -- battleground name from BattlemasterList.dbc. The menu lives in the shared DropDownList1 frame,
    -- which RelabelAll does not scan, so relabel it after the native OnClick builds the menu.
    if MiniMapBattlefieldFrame.HookScript then
        MiniMapBattlefieldFrame:HookScript("OnClick", function()
            if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
                return
            end
            local from = LocalizedAcherusName()
            if not from then
                return
            end
            Relabel(DropDownList1, from, TITLE)
            Relabel(DropDownList2, from, TITLE)
        end)
    end
end

-- The top bar row is a localized format ("Bases: N  Victory Points: N/1600" for the Eye of the Storm), so the
-- "Bases" label cannot be matched as text. Rewrite the leading "<label>:" of each always-up row instead,
-- whatever locale, and let the client keep filling the numbers.
local function FixAlwaysUp()
    for i = 1, (NUM_ALWAYS_UP_UI_FRAMES or 4) do
        local fs = _G["AlwaysUpFrame" .. i .. "Text"]
        if fs and fs.GetText and fs.SetText then
            local text = fs:GetText()
            if type(text) == "string" and string.find(text, ":", 1, true) then
                fs:SetText((string.gsub(text, "^.-:", "Orbs:", 1)))
            end
        end
    end
end

local function RelabelAll()
    if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
        return
    end

    -- top bar label, locale independent (no text matching); the score column is handled by the
    -- GetBattlefieldStatInfo wrapper above
    FixAlwaysUp()

    -- battleground frames, minimap tooltip and dropdown title: the name comes from BattlemasterList.dbc, so
    -- match the localized name the client itself reports (nil outside the queue/match: nothing to relabel)
    local from = LocalizedAcherusName()
    if from then
        Relabel(BattlefieldFrame, from, TITLE)
        Relabel(DropDownList1, from, TITLE)
        Relabel(DropDownList2, from, TITLE)
    end

    HookMinimap()
end

-- the server's active toggle calls this right after flipping AcherusBG_UI.active, so the relabel is applied
-- immediately instead of waiting for the next frame update (world states only tick every 5 seconds)
AcherusBG_UI.Relabel = RelabelAll

RelabelAll()

-- Reapply whenever the client rebuilds the affected frames.
if WorldStateAlwaysUpFrame_Update then
    hooksecurefunc("WorldStateAlwaysUpFrame_Update", RelabelAll)
end
if WorldStateFrame_Update then
    hooksecurefunc("WorldStateFrame_Update", RelabelAll)
end
if WorldStateScoreFrame_Update then
    hooksecurefunc("WorldStateScoreFrame_Update", RelabelAll)
end
if WorldStateScoreFrame_Show then
    hooksecurefunc("WorldStateScoreFrame_Show", RelabelAll)
end
if PVPFrame_Update then
    hooksecurefunc("PVPFrame_Update", RelabelAll)
end
if BattlefieldFrame_Update then
    hooksecurefunc("BattlefieldFrame_Update", RelabelAll)
end

local watcher = CreateFrame("Frame")
watcher:RegisterEvent("UPDATE_WORLD_STATES")
watcher:RegisterEvent("WORLD_STATE_UI_TIMER_UPDATE")
watcher:RegisterEvent("UPDATE_BATTLEFIELD_STATUS")
watcher:RegisterEvent("UPDATE_BATTLEFIELD_SCORE")
watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
watcher:SetScript("OnEvent", RelabelAll)

-- ---------------------------------------------------------------------------- PvP battleground list
-- The client's Battlegrounds tab (PVPBattlegroundFrame) lists the battleground types of BattlemasterList.dbc,
-- so a custom mode the client has no type for cannot come from the server. Expose one synthetic entry by
-- wrapping the two APIs that list reads (GetNumBattlegroundTypes/GetBattlegroundInfo): the native update then
-- counts it, draws its row and includes it in the scroll range like any other battleground, so it scrolls
-- with the list. The Eye of the Storm type is the one the server fakes the Acherus match with, so the queue
-- icon must be moved from that row to ours.

local ACHERUS_BG_TEXTURE = "Interface\\PVPFrame\\PvpRandomBg" -- the same art the Random Battleground uses
local ACHERUS_BG_LORE =
    "The runeforges of Acherus have become the prize of a bitter quarrel.\n\n\nThe Knights of the Ebon Blade " ..
    "have turned on one another over who will command them, and the Alliance and the Horde have seized the " ..
    "chance to exploit that schism - each faction intent on turning the death knights' strife to its own gain."

local OrigGetNumBattlegroundTypes = GetNumBattlegroundTypes
local OrigGetBattlegroundInfo = GetBattlegroundInfo

-- our synthetic entry is appended after every real type (its index follows the sorted DBC list)
local function AcherusBGIndex()
    return (OrigGetNumBattlegroundTypes and OrigGetNumBattlegroundTypes() or 0) + 1
end

if OrigGetNumBattlegroundTypes and OrigGetBattlegroundInfo then
    GetNumBattlegroundTypes = function()
        return OrigGetNumBattlegroundTypes() + 1
    end
    GetBattlegroundInfo = function(index)
        if index == AcherusBGIndex() then
            -- name, canEnter, isHoliday, isRandom, BattleGroundID (an id outside PVPBATTLEGROUND_TEXTURELIST)
            return TITLE, true, false, false, 999
        end
        return OrigGetBattlegroundInfo(index)
    end
end

-- the art is the very texture the Random Battleground entry shows, so its size, position, alpha and layer are
-- exactly those of the other battlegrounds (the native update only swaps the texture file)
local function ApplyAcherusInfo()
    if PVPBattlegroundFrameBGTex then
        PVPBattlegroundFrameBGTex:SetTexture(ACHERUS_BG_TEXTURE)
        PVPBattlegroundFrameBGTex:SetTexCoord(0.0, 0.6504, 0.0, 0.9414) -- the crop the XML applies
        PVPBattlegroundFrameBGTex:Show()
    end
    local desc = PVPBattlegroundFrameInfoScrollFrameChildFrameDescription
    if desc then
        desc:SetText(ACHERUS_BG_LORE)
        desc:Show()
    end
    local rewards = PVPBattlegroundFrameInfoScrollFrameChildFrameRewardsInfo
    if rewards then
        rewards:Hide()
    end
    if PVPBattlegroundFrameInfoScrollFrame then
        PVPBattlegroundFrameInfoScrollFrame:SetVerticalScroll(0)
    end
end

-- intercept our entry before the native path (which would query GetBattlefieldInfo and get nothing useful)
if PVPBattleground_UpdateInfo then
    local OrigUpdateInfo = PVPBattleground_UpdateInfo
    PVPBattleground_UpdateInfo = function(BGindex)
        if BGindex == nil and PVPBattlegroundFrame then
            BGindex = PVPBattlegroundFrame.selectedBG
        end
        if BGindex == AcherusBGIndex() then
            ApplyAcherusInfo()
            return
        end
        OrigUpdateInfo(BGindex)
    end
end

-- mirror the native queued/confirm icon onto our row and keep it off the real battleground rows while the
-- queue is Acherus (AcherusBG_UI.active is only on for this mode)
local function AcherusQueueStatus()
    local row
    for i = 1, (NUM_DISPLAYED_BATTLEGROUNDS or 5) do
        local b = _G["BattlegroundType" .. i]
        if b then
            if b.BGindex == AcherusBGIndex() then
                row = b
            elseif AcherusBG_UI.active and b.status then
                b.status:Hide()
            end
        end
    end

    if not row or not row.status then
        return
    end

    row.status:Hide()
    if not AcherusBG_UI.active then
        return
    end

    local from = LocalizedAcherusName()
    for i = 1, (MAX_BATTLEFIELD_QUEUES or 2) do
        local status, mapName = GetBattlefieldStatus(i)
        if status and status ~= "none" and mapName == from then
            if status == "queued" then
                row.status.texture:SetTexture("Interface\\PVPFrame\\PVP-Currency-" ..
                    (UnitFactionGroup("player") or "Alliance"))
                row.status.texture:SetTexCoord(0.0, 1.0, 0.0, 1.0)
                row.status.tooltip = BATTLEFIELD_QUEUE_STATUS
                row.status:Show()
            elseif status == "confirm" then
                row.status.texture:SetTexture("Interface\\CharacterFrame\\UI-StateIcon")
                row.status.texture:SetTexCoord(0.45, 0.95, 0.0, 0.5)
                row.status.tooltip = BATTLEFIELD_CONFIRM_STATUS
                row.status:Show()
            end
            break
        end
    end
end

if PVPBattleground_UpdateQueueStatus then
    hooksecurefunc("PVPBattleground_UpdateQueueStatus", AcherusQueueStatus)
end

-- the native Join queues "first available"; for our entry send the module command instead
local function AcherusJoin(self, button, down)
    if PVPBattlegroundFrame and PVPBattlegroundFrame.selectedBG == AcherusBGIndex() then
        SendChatMessage(".acherus queue", "SAY")
        return
    end
    if self.AcherusOrigOnClick then
        self.AcherusOrigOnClick(self, button, down)
    end
end

for _, buttonName in ipairs({ "PVPBattlegroundFrameJoinButton", "PVPBattlegroundFrameGroupJoinButton" }) do
    local button = _G[buttonName]
    if button and button.GetScript and button.SetScript then
        local original = button:GetScript("OnClick")
        if original then
            button.AcherusOrigOnClick = original
            button:SetScript("OnClick", AcherusJoin)
        end
    end
end

-- the panel does not re-render the info for our entry on show (it only requests the real type instances)
if PVPBattlegroundFrame and PVPBattlegroundFrame.HookScript then
    PVPBattlegroundFrame:HookScript("OnShow", function()
        if PVPBattlegroundFrame.selectedBG == AcherusBGIndex() then
            ApplyAcherusInfo()
        end
    end)
end

-- ---------------------------------------------------------------------------- orb carrier auras
-- The server applies a visible "Portal State" dummy aura to each orb carrier (33338 red/Blood, 33339
-- green/Unholy, 33340 blue/Frost). All three share the same DBC icon, so they are told apart by the
-- (localized) spell name queried once from the client, then relabeled with death knight icons. This keeps
-- a death knight from ending up with two identical presence icons in the aura bar.

local ORB_ICON_BLOOD = "Interface\\Icons\\Spell_Deathknight_BladedArmor"
local ORB_ICON_UNHOLY = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade"
local ORB_ICON_FROST = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade2"
local ORB_DESC = "Carrying an orb from the runeforges of Acherus."

local ORB_AURAS = {}
do
    local defs = {
        { 33338, "Blood Orb", ORB_ICON_BLOOD },
        { 33339, "Unholy Orb", ORB_ICON_UNHOLY },
        { 33340, "Frost Orb", ORB_ICON_FROST },
    }
    for _, def in ipairs(defs) do
        local spellName = GetSpellInfo(def[1])
        if spellName then
            ORB_AURAS[spellName] = {
                name = def[2],
                icon = def[3],
                pattern = string.gsub(spellName, "%W", "%%%0"),
            }
        end
    end
end

-- the orb aura is always a helpful buff, so the buff list works for every frame (a filter-less UnitAura
-- would not necessarily match the index the frame used). The count is the stack amount the server keeps on
-- the portal aura (its charges, since the portal is not stackable in the DBC).
local function OrbAuraFor(unit, index)
    local name, _, _, count = UnitBuff(unit, index)
    return name and ORB_AURAS[name], count or 0
end

-- player buff frame; "buttonName" is the button name prefix, "BuffButton" for buffs
hooksecurefunc("AuraButton_Update", function(buttonName, index)
    if not AcherusBG_UI.active or buttonName ~= "BuffButton" then
        return
    end
    local def = OrbAuraFor("player", index)
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
        local def = OrbAuraFor(self.unit, i)
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
        local def = OrbAuraFor(unit, i)
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
            local def = ORB_AURAS[name]
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

-- Aura tooltips: the player buffs use SetUnitAura, the other frames use SetUnitBuff. The aura description
-- has no Lua API, so it is appended here; Show() afterwards recomputes the tooltip height. The current totals
-- are derived from the stack count the server mirrors on the aura (see OrbAuraFor).
local function OrbTooltip(self, unit, index)
    if not AcherusBG_UI.active then
        return
    end
    local def, count = OrbAuraFor(unit, index)
    if not def then
        return
    end
    local line = _G[self:GetName() .. "TextLeft1"]
    if line then
        line:SetText(def.name)
    end

    local stacks = (count and count > 0) and count or 1
    self:AddLine(ORB_DESC, 1, 1, 1)
    self:AddLine(string.format("Damage done: +%d%%", math.min(100, 20 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Damage taken: +%d%%", math.min(100, 20 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Healing taken: -%d%%", math.min(50, 10 * stacks)), 1, 1, 1)
    self:AddLine(string.format("Size: +%d%%", math.min(100, 20 + 20 * (stacks - 1))), 1, 1, 1)
    self:AddLine(string.format("(%d stack%s, +1 every 15 sec)", stacks, stacks == 1 and "" or "s"), 1, 1, 1)
    self:Show()
end

hooksecurefunc(GameTooltip, "SetUnitAura", OrbTooltip)
hooksecurefunc(GameTooltip, "SetUnitBuff", OrbTooltip)

-- floating combat text: the aura name is already baked into the message string, so rewrite it in the funnel
if CombatText_AddMessage then
    local OrbCombatText = CombatText_AddMessage
    CombatText_AddMessage = function(message, ...)
        if AcherusBG_UI.active and type(message) == "string" then
            for _, def in pairs(ORB_AURAS) do
                if string.find(message, def.pattern) then
                    message = string.gsub(message, def.pattern, def.name)
                end
            end
        end
        return OrbCombatText(message, ...)
    end
end

-- On the inactive -> active transition, force the already drawn frames to re-render once. After a /reload
-- the buff bar is built with the portal icon before the payload arrives, and our per-frame hooks only run
-- on the next aura update (which never comes for a permanent aura). Re-triggering the native updates runs
-- the hooks immediately, so the rune appears without waiting.
local orbsWereActive = false

local function RefreshOrbAuras()
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
    if active and not orbsWereActive then
        RefreshOrbAuras()
    end
    orbsWereActive = active
end

-- The server flips AcherusBG_UI.active and calls Relabel() right after (re)applying the payload, so wrap
-- the entry point defined above to catch that transition.
local RelabelBase = AcherusBG_UI.Relabel
if RelabelBase then
    AcherusBG_UI.Relabel = function()
        OnActiveChanged()
        local result = RelabelBase()
        -- the battlefield status can arrive before the active flag flips, leaving the list queue icon on the
        -- Eye of the Storm row: re-place it now instead of waiting for the next list update
        AcherusQueueStatus()
        return result
    end
end

-- ---------------------------------------------------------------------------- minimap orb markers
-- The server pushes AcherusBG_Orbs.Update(px, py, x, y, name, ...) every 0.25 s through the same addon
-- message channel. Positions are world coordinates (yards). The Ebon Hold world map is rotated 90 degrees
-- and mirrored on the minimap (its X comes from the world Y, its Y from the world X), so a world offset
-- (dx, dy) maps to the minimap as (-dy, dx), scaled by the minimap's own world span.

local ORB_PRESENCE_ICON = {
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

AcherusBG_Orbs = { data = nil, icons = {} }

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
    AcherusBG_Orbs.icons[i] = CreateMarker(ORB_PRESENCE_ICON[i])
end

local function HideOrbMarkers()
    for i = 1, 3 do
        AcherusBG_Orbs.icons[i]:Hide()
    end
end

-- a same team orb carrier is a battlefield raid member: find its unit token by name so the marker can
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

local orbDriver = CreateFrame("Frame")
orbDriver:SetScript("OnUpdate", function()
    local data = AcherusBG_Orbs.data
    if not data or not AcherusBG_UI.active or (GetTime() - data.time) > 2 then
        HideOrbMarkers()
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
        local orb = data.orbs[i]
        local icon = AcherusBG_Orbs.icons[i]
        if orb then
            -- a same team carrier follows its battlefield raid unit for a smooth position; the others
            -- (enemy carriers and the forges) use the last server position
            local orbX, orbY = orb.x, orb.y
            if orb.unit then
                local ux, uy = GetPlayerMapPosition(orb.unit)
                if ux and not (ux == 0 and uy == 0) then
                    orbY = MAP_A + MAP_B * ux
                    orbX = MAP_C + MAP_D * uy
                end
            end

            Place(icon, orbX, orbY)
        else
            icon:Hide()
        end
    end
end)
orbDriver:Show()

-- Called by the server every 0.25 s with the player position (the anchor) and each orb's position, plus
-- (for a carrier of the observer's team) its name so the client can follow that unit smoothly.
function AcherusBG_Orbs.Update(px, py, fx, fy, fName, bx, by, bName, ux, uy, uName)
    local function Store(x, y, name)
        return { x = x, y = y, unit = FindUnitByName(name) }
    end

    AcherusBG_Orbs.data = {
        time = GetTime(),
        px = px,
        py = py,
        orbs = {
            Store(fx, fy, fName),
            Store(bx, by, bName),
            Store(ux, uy, uName),
        },
    }
end

-- Tell the worldserver the script was applied, so it can log it (and stop pinging this client).
-- The body is valid Lua: it would stay a harmless no-op if the message were ever echoed back.
SendAddonMessage('AcherusBG', 'return 2', 'GUILD')
