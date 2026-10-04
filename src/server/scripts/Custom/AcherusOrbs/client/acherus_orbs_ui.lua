-- Battle for Acherus: client-side UI relabels.
--
-- Pushed by the worldserver through Warden (bootstrap listener) + addon messages, see the module README.
-- Runs in the client global environment via loadstring. Keep it idempotent: the AcherusBG_UI guard prevents a
-- second execution from stacking hooks when the payload is delivered again (for example after a version
-- update or an extra handshake).

if AcherusBG_UI then return end
AcherusBG_UI = { active = false }

local TITLE = "Battle for Acherus"

-- The server only turns the relabel on while the player is in the Acherus queue or match (AcherusBG_UI.active).
-- Belt-and-suspenders: never touch the real Eye of the Storm instance, even if a toggle was missed.
local function IsRealEyeOfTheStorm()
    local _, instanceType = IsInInstance()
    return instanceType == "pvp" and GetRealZoneText() == "Eye of the Storm"
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

        for i = 1, GameTooltip:NumLines() do
            local line = _G["GameTooltipTextLeft" .. i]
            if line and line.GetText and line.SetText then
                local text = line:GetText()
                if type(text) == "string" and string.find(text, "Eye of the Storm", 1, true) then
                    line:SetText((string.gsub(text, "Eye of the Storm", TITLE)))
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
            Relabel(DropDownList1, "Eye of the Storm", TITLE)
            Relabel(DropDownList2, "Eye of the Storm", TITLE)
        end)
    end
end

local function RelabelAll()
    if not AcherusBG_UI.active or IsRealEyeOfTheStorm() then
        return
    end

    -- top score bar of the fake Eye of the Storm frame: "Bases" -> "Orbs"
    Relabel(WorldStateAlwaysUpFrame, "Bases", "Orbs")
    Relabel(WorldStateFrame, "Bases", "Orbs")

    -- final scoreboard objective column: "Flag Captures" -> "Points"
    Relabel(WorldStateScoreFrame, "Flag Captures", "Points")
    Relabel(ScoreboardFrame, "Flag Captures", "Points")

    -- PvP / battleground queue frames: the battleground name comes from BattlemasterList.dbc
    Relabel(PVPFrame, "Eye of the Storm", TITLE)
    Relabel(BattlefieldFrame, "Eye of the Storm", TITLE)
    Relabel(PVPBattlefieldFrame, "Eye of the Storm", TITLE)

    -- shared dropdown menu frames (right-click on the minimap battleground button)
    Relabel(DropDownList1, "Eye of the Storm", TITLE)
    Relabel(DropDownList2, "Eye of the Storm", TITLE)

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

-- Tell the worldserver the script was applied, so it can log it (and stop pinging this client).
-- The body is valid Lua: it would stay a harmless no-op if the message were ever echoed back.
SendAddonMessage('AcherusBG', 'return 2', 'GUILD')
