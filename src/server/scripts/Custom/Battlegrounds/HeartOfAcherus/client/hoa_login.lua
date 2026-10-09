-- Heart of Acherus: client UI part 1 (login), see docs/client-ui.md.
--
-- Sent to every player on login. The AcherusBG_UI guard keeps it idempotent, so a resend never stacks hooks.
-- Part 2 (hoa_match.lua) extends the helpers exposed on AcherusBG_UI.

if AcherusBG_UI then return end
AcherusBG_UI = { active = false }

local TITLE = "Heart of Acherus"

-- the localized name of the faked Eye of the Storm, as the minimap, dropdown and list show it (never hardcoded)
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

-- AcherusBG_UI.active is on while queued too, but the top bar and the score column only belong to the match (the
-- fake status is "active" there): queued elsewhere, that zone's world state UI is left alone
local function IsInAcherusMatch()
    local name = LocalizedAcherusName()
    if not name then
        return false
    end
    for i = 1, (MAX_BATTLEFIELD_QUEUES or 2) do
        local status, mapName = GetBattlefieldStatus(i)
        if status == "active" and mapName == name then
            return true
        end
    end
    return false
end

-- safety net: never touch the real Eye of the Storm, even if a toggle was missed (works in every locale)
local function IsRealEyeOfTheStorm()
    local _, instanceType = IsInInstance()
    if instanceType ~= "pvp" then
        return false
    end
    local name = LocalizedAcherusName()
    return name ~= nil and GetRealZoneText() == name
end

-- our scoreboard column instead of the Eye of the Storm one; the empty icon makes the client draw the plain number
local POINTS_TOOLTIP = "Points earned by holding runes and killing enemies."

if GetBattlefieldStatInfo then
    local OrigBattlefieldStatInfo = GetBattlefieldStatInfo
    GetBattlefieldStatInfo = function(index)
        if AcherusBG_UI.active and IsInAcherusMatch() and index == 1 then
            return "Points", "", POINTS_TOOLTIP
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

-- Minimap battleground button tooltip and dropdown. Hooked lazily (the button may not exist yet), through the
-- global MiniMapBattlefieldFrame_OnUpdate the XML OnEnter resolves, so the OnEnter/OnLeave churn cannot wipe it.
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

    -- the right-click dropdown title, once the native OnClick built the menu
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

-- the top bar rows are a localized format ("Bases: N  Victory Points: N/1600"): rewrite the leading "<label>:"
local function FixAlwaysUp()
    for i = 1, (NUM_ALWAYS_UP_UI_FRAMES or 4) do
        local fs = _G["AlwaysUpFrame" .. i .. "Text"]
        if fs and fs.GetText and fs.SetText then
            local text = fs:GetText()
            if type(text) == "string" and string.find(text, ":", 1, true) then
                fs:SetText((string.gsub(text, "^.-:", "Runes:", 1)))
            end
        end
    end
end

local function RelabelAll()
    if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
        return
    end

    if IsInAcherusMatch() then
        FixAlwaysUp()
    end

    -- nil outside the queue/match: nothing to relabel
    local from = LocalizedAcherusName()
    if from then
        Relabel(BattlefieldFrame, from, TITLE)
        Relabel(DropDownList1, from, TITLE)
        Relabel(DropDownList2, from, TITLE)
    end

    HookMinimap()
end

-- called by the server right after flipping AcherusBG_UI.active
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
-- The Battlegrounds tab lists the BattlemasterList.dbc types. A synthetic entry comes from wrapping the APIs the
-- list reads (GetNumBattlegroundTypes/GetBattlegroundInfo), so the native update draws and scrolls it; the queue
-- icon moves from the faked Eye of the Storm row to ours.

local ACHERUS_BG_TEXTURE = "Interface\\PVPFrame\\PvpRandomBg" -- the same art the Random Battleground uses
local ACHERUS_BG_LORE =
    "The runeforges of Acherus have become the prize of a bitter quarrel.\n\n\nThe Knights of the Ebon Blade " ..
    "have turned on one another over who will command them, and the Alliance and the Horde have seized the " ..
    "chance to exploit that schism - each faction intent on turning the death knights' strife to its own gain."

local OrigGetNumBattlegroundTypes = GetNumBattlegroundTypes
local OrigGetBattlegroundInfo = GetBattlegroundInfo

-- second in the list, after the Random Battleground; GetBattlegroundInfo and RequestBattlegroundInstanceInfo shift
-- the real types after ours by one
local ACHERUS_BG_POSITION = 2

local function AcherusBGIndex()
    return math.min(ACHERUS_BG_POSITION, (OrigGetNumBattlegroundTypes and OrigGetNumBattlegroundTypes() or 0) + 1)
end

-- the client index of a list index, nil for our entry
local function RealBGIndex(index)
    local ours = AcherusBGIndex()
    if type(index) ~= "number" or index < ours then
        return index
    elseif index == ours then
        return nil
    end
    return index - 1
end

if OrigGetNumBattlegroundTypes and OrigGetBattlegroundInfo then
    GetNumBattlegroundTypes = function()
        return OrigGetNumBattlegroundTypes() + 1
    end
    GetBattlegroundInfo = function(index)
        local real = RealBGIndex(index)
        if not real then
            -- name, canEnter, isHoliday, isRandom, BattleGroundID (an id outside PVPBATTLEGROUND_TEXTURELIST)
            return TITLE, true, false, false, 999
        end
        return OrigGetBattlegroundInfo(real)
    end
end

-- our entry has no client instance info to request; the Join buttons send the module command for it
local OrigRequestBattlegroundInstanceInfo = RequestBattlegroundInstanceInfo
if OrigRequestBattlegroundInstanceInfo then
    RequestBattlegroundInstanceInfo = function(index)
        local real = RealBGIndex(index)
        if real then
            return OrigRequestBattlegroundInstanceInfo(real)
        end
    end
end

-- the Random Battleground art, so size, position, alpha and layer match the other battlegrounds
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

-- the row button's name FontString (the list recycles the buttons while scrolling)
local function RowNameText(row)
    local text = row.title or (row.GetFontString and row:GetFontString())
    if not text and row.GetName and row:GetName() then
        text = _G[row:GetName() .. "Text"]
    end
    return text
end

-- the shown row with our title; BGindex alone is ambiguous when the list was built without the wrapper
local function IsAcherusRow(row)
    if not row or not row:IsShown() or row.BGindex ~= AcherusBGIndex() then
        return false
    end
    local name = RowNameText(row)
    return name ~= nil and name.GetText ~= nil and name:GetText() == TITLE
end

-- mirror the native queued/confirm icon onto our row and keep it off the real battleground rows while the
-- queue is Acherus (AcherusBG_UI.active is only on for this mode)
local function AcherusQueueStatus()
    local row
    for i = 1, (NUM_DISPLAYED_BATTLEGROUNDS or 5) do
        local b = _G["BattlegroundType" .. i]
        if b then
            if IsAcherusRow(b) then
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

-- "NEW" badge after our name; the rows are recycled while scrolling, so it follows whichever row shows our entry
local NEW_BADGE_TEXT = "NEW"

-- each row gets its own badge (created once); only the row showing our entry shows it
local function AcherusNewBadge()
    for i = 1, (NUM_DISPLAYED_BATTLEGROUNDS or 5) do
        local row = _G["BattlegroundType" .. i]
        if row then
            local name = RowNameText(row)
            local ours = IsAcherusRow(row)
            if ours and not row.acherusNewBadge then
                local badge = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
                badge:SetText(NEW_BADGE_TEXT)
                badge:SetTextColor(0.25, 1.0, 0.25)
                row.acherusNewBadge = badge
            end

            local badge = row.acherusNewBadge
            if badge then
                if ours then
                    -- the name FontString can be wider than its text, so the badge follows the text width
                    badge:ClearAllPoints()
                    badge:SetPoint("LEFT", name, "LEFT", name:GetStringWidth() + 6, 0)
                    badge:Show()
                else
                    badge:Hide()
                end
            end
        end
    end
end

if PVPBattleground_UpdateQueueStatus then
    hooksecurefunc("PVPBattleground_UpdateQueueStatus", AcherusNewBadge)
end
if PVPBattleground_UpdateBattlegrounds then
    hooksecurefunc("PVPBattleground_UpdateBattlegrounds", AcherusNewBadge)
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

-- redraws the list with our entry: the tab may have been open when this part was applied
local function RefreshBattlegroundList()
    if PVPBattleground_UpdateBattlegrounds then
        pcall(PVPBattleground_UpdateBattlegrounds)
    end
    if PVPBattleground_UpdateQueueStatus then
        pcall(PVPBattleground_UpdateQueueStatus)
    end
end

-- true if a visible row already shows our synthetic entry (the list was built with the wrapper in place)
local function AcherusRowShown()
    for i = 1, (NUM_DISPLAYED_BATTLEGROUNDS or 5) do
        if IsAcherusRow(_G["BattlegroundType" .. i]) then
            return true
        end
    end
    return false
end

-- the panel does not re-render the info for our entry on show (it only requests the real type instances)
if PVPBattlegroundFrame and PVPBattlegroundFrame.HookScript then
    PVPBattlegroundFrame:HookScript("OnShow", function()
        if PVPBattlegroundFrame.selectedBG == AcherusBGIndex() then
            ApplyAcherusInfo()
        end
        if not AcherusRowShown() then
            RefreshBattlegroundList()
        end
    end)
end

-- already open: rebuild now, without the AcherusRowShown gate (the index alone cannot tell)
if PVPBattlegroundFrame and PVPBattlegroundFrame:IsShown() then
    RefreshBattlegroundList()
end

-- Shared helpers the match-only part (hoa_match.lua) reads from this table.
AcherusBG_UI.TITLE = TITLE
AcherusBG_UI.LocalizedAcherusName = LocalizedAcherusName
AcherusBG_UI.IsInAcherusMatch = IsInAcherusMatch
AcherusBG_UI.IsRealEyeOfTheStorm = IsRealEyeOfTheStorm
AcherusBG_UI.AcherusQueueStatus = AcherusQueueStatus

-- PLAYER_LOGOUT fires on logout and /reload: the server tells them apart by the next status request
local logoutWatcher = CreateFrame("Frame")
logoutWatcher:RegisterEvent("PLAYER_LOGOUT")
logoutWatcher:SetScript("OnEvent", function()
    SendAddonMessage('AcherusBG', 'return -1', 'WHISPER', UnitName('player'))
end)

-- ack: part 1 applied (whisper to self works without a guild; the body is valid Lua if echoed back)
SendAddonMessage('AcherusBG', 'return 1', 'WHISPER', UnitName('player'))
