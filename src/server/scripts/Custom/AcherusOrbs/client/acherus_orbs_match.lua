-- Heart of Acherus: match-only client-side UI (part 2).
--
-- Sent by the worldserver when the player enters the Heart of Acherus queue, and re-sent after a /reload if
-- the player is still queued or in a match. It extends the shared helpers exposed by acherus_orbs_login.lua
-- (part 1), which must be applied first. Runs in the client global environment via loadstring; the
-- AcherusBG_Part2 guard keeps it idempotent (the match section is sent again whenever it is missing).

if not AcherusBG_UI then return end
if AcherusBG_Part2 then return end
AcherusBG_Part2 = true

local IsRealEyeOfTheStorm = AcherusBG_UI.IsRealEyeOfTheStorm

-- ---------------------------------------------------------------------------- rune carrier auras
-- The server applies a visible "Portal State" dummy aura to each rune carrier (33338 red/Blood, 33339
-- green/Unholy, 33340 blue/Frost). All three share the same DBC icon, so they are told apart by the
-- (localized) spell name queried once from the client, then relabeled with death knight icons. This keeps
-- a death knight from ending up with two identical presence icons in the aura bar.

local ORB_ICON_BLOOD = "Interface\\Icons\\Spell_Deathknight_BladedArmor"
local ORB_ICON_UNHOLY = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade"
local ORB_ICON_FROST = "Interface\\Icons\\Spell_Deathknight_EmpowerRuneblade2"
local ORB_DESC = "Carrying a rune from the runeforges of Acherus."

local ORB_AURAS = {}
do
    local defs = {
        { 33338, "Blood Rune", ORB_ICON_BLOOD },
        { 33339, "Unholy Rune", ORB_ICON_UNHOLY },
        { 33340, "Frost Rune", ORB_ICON_FROST },
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

-- the rune aura is always a helpful buff, so the buff list works for every frame (a filter-less UnitAura
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
    self:AddLine(string.format("Healing received: -%d%%", math.min(50, 10 * stacks)), 1, 1, 1)
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
        if AcherusBG_UI.AcherusQueueStatus then
            AcherusBG_UI.AcherusQueueStatus()
        end
        return result
    end
end

-- ---------------------------------------------------------------------------- minimap rune markers
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

-- Called by the server every 0.25 s with the player position (the anchor) and each rune's position, plus
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

-- ---------------------------------------------------------------------------- mount methods
-- The client refuses mount spells indoors on its own: in the hall the mount buttons are disabled and nothing reaches
-- the server. The server picks how players mount in the hall (AcherusOrbs.MountMethod) and puts its id in
-- AcherusBG_MountMethod at the top of this payload; each method below only acts while its id is the active one.

local MOUNT_METHOD_FRAME = 1

-- ---------------------------------------------------------------------------- mount method 1: frame
-- During a match this button asks the server for the Acherus Deathcharger instead; the server casts it
-- (usual cast time, combat and movement rules), or dismounts the player when mounted. Bindable with
-- /click AcherusBGMountButton. Dragging it moves it; the position is kept in a client CVar (Config.wtf), since the
-- script has no SavedVariables and the layout cache does not restore frames created after the login.

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
    SendAddonMessage('AcherusBG', 'mount', 'WHISPER', UnitName('player'))
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

-- the rune carrier aura (see ORB_AURAS), death, combat and the forms that forbid mounts (druid forms, Ghost Wolf)
-- grey the icon out like an unusable action; it stays clickable and the server tells why. Mounted, it dismounts.
local function CanMount()
    if UnitIsDeadOrGhost("player") or UnitAffectingCombat("player") then
        return false
    end

    local _, class = UnitClass("player")
    if (class == "DRUID" or class == "SHAMAN") and GetShapeshiftForm() ~= 0 then
        return false
    end

    for spellName in pairs(ORB_AURAS) do
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

-- shown while the player is in an Acherus match: the server fakes it as an active battlefield
local function UpdateMountButton()
    local inMatch = false
    if AcherusBG_MountMethod == MOUNT_METHOD_FRAME and AcherusBG_UI.active and not IsRealEyeOfTheStorm() then
        for i = 1, (MAX_BATTLEFIELD_QUEUES or 2) do
            if GetBattlefieldStatus(i) == "active" then
                inMatch = true
                break
            end
        end
    end

    if inMatch then
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

-- ---------------------------------------------------------------------------- instruction book
-- The book of each starting area is a page text gameobject, shown by ItemTextFrame. Its page is a SimpleHTML that
-- has no font for H1, so the title looks like any paragraph, and it cannot draw icons. While our book is open the
-- H1 gets a bigger copy of the page font, and a row of death knight runes is drawn under the title of the first
-- page (the page leaves blank lines for it). Any other book gets the page font back for H1.

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

-- Tell the worldserver the match part was applied (bit 2 set, part 1 was already required to get here). Sent
-- through a whisper to self so it reaches the server without a guild. The body is valid Lua if echoed back.
SendAddonMessage('AcherusBG', 'return 3', 'WHISPER', UnitName('player'))
