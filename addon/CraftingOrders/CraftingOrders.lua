-----------------------------------------------------------------------
-- Crafting Orders Addon — Client-side UI
-- Mirrors the standard Blizzard_TradeSkillUI look & feel, sourcing data
-- from the CraftingOrder AddonMessage protocol instead of the default
-- GetTradeSkill* API.
-----------------------------------------------------------------------

local CRAFTING_ORDER_PROTOCOL_VERSION = 2;
local CraftingOrderRequestId = 0;
local CraftingOrderChunkState = { requestId = 0, opcode = "", page = 0, totalPages = 1, total = 0, parts = {} };

-- Stub out the missing Blizzard trainer function to prevent XML load-time crashes
if not ClassTrainerSkillButton_OnClick then
    ClassTrainerSkillButton_OnClick = function() end
end

-- The compact frame reserves nine standard 16px skill rows. Keep
-- FauxScrollFrame and button rendering in lockstep with this value.
TRADE_SKILLS_DISPLAYED = 9;
MAX_TRADE_SKILL_REAGENTS = 8;
TRADE_SKILL_HEIGHT = 16;
TRADE_SKILL_TEXT_WIDTH = 275;

CraftingOrderTypeColor = { };
CraftingOrderTypeColor["optimal"]  = { r = 1.00, g = 0.50, b = 0.25 };
CraftingOrderTypeColor["medium"]   = { r = 1.00, g = 1.00, b = 0.00 };
CraftingOrderTypeColor["easy"]     = { r = 0.25, g = 0.75, b = 0.25 };
CraftingOrderTypeColor["trivial"]  = { r = 0.50, g = 0.50, b = 0.50 };
CraftingOrderTypeColor["header"]   = { r = 1.00, g = 0.82, b = 0 };

-----------------------------------------------------------------------
-- State & utility
-----------------------------------------------------------------------

local SERVER_PREFIXES = {
    INIT = true,
    RECIPES = true,
    RECIPES_BEGIN = true,
    RECIPES_PART = true,
    RECIPES_END = true,
    CRAFT_RESULT = true,
    DISENCHANT_RESULT = true,
    MILLING_RESULT = true,
    PROSPECTING_RESULT = true,
    ENCHANT_SLOTS = true,
    ENCHANT_TARGETS = true,
    ENCHANT_RESULT = true,
    CLOSE_UI = true,
};

local CLIENT_PREFIXES = {
    "REQUEST_RECIPES",
    "REQUEST_DISENCHANT_ITEMS",
    "REQUEST_MILLING_ITEMS",
    "REQUEST_PROSPECTING_ITEMS",
    "CRAFT",
    "DISENCHANT",
    "MILL",
    "PROSPECT",
    "REQUEST_ENCHANT_TARGETS",
    "ENCHANT",
    "CLOSE",
};

local function RegisterCraftingOrderPrefixes()
    if RegisterAddonMessagePrefix then
        RegisterAddonMessagePrefix("CO");
        for prefix in pairs(SERVER_PREFIXES) do
            RegisterAddonMessagePrefix(prefix);
        end
        for _, prefix in ipairs(CLIENT_PREFIXES) do
            RegisterAddonMessagePrefix(prefix);
        end
    end
end

RegisterCraftingOrderPrefixes();

State = {
    npcGuid        = 0,
    professionId   = 0,
    professionName = "",
    isEnchantMode  = false,
    isDisenchantMode = false,
    isMillingMode = false,
    isProspectingMode = false,
    isHandInMode = false,
    selectingEnchantTarget = false,
    selectedEnchantSpellId = nil,
    selectedEnchantSpellName = nil,
    baseEnchantRecipes = nil,
    recipes        = {},
    selectedRecipeIndex = 0,
    onlyMakeable   = false,
    filterText     = "",
    subclassFilter = "ALL",
    categoryFilter = "ALL",
    subclasses     = { "ALL" },
    categories     = { "ALL" },
    collapsedHeaders = {},
    recipeBuffer    = "",
    rangeCheckElapsed = 0,
};

local GetFilteredRecipes;
local GetFilteredRows;
local SetCollapseAllButtonState;

local EQUIP_CATEGORY_LABELS = {
    INVTYPE_HEAD           = "Head",
    INVTYPE_NECK           = "Neck",
    INVTYPE_SHOULDER       = "Shoulders",
    INVTYPE_BODY           = "Shirt",
    INVTYPE_CHEST          = "Chest",
    INVTYPE_ROBE           = "Chest",
    INVTYPE_WAIST          = "Waist",
    INVTYPE_LEGS           = "Legs",
    INVTYPE_FEET           = "Boots",
    INVTYPE_WRIST          = "Wrists",
    INVTYPE_HAND           = "Hands",
    INVTYPE_FINGER         = "Rings",
    INVTYPE_TRINKET        = "Trinkets",
    INVTYPE_CLOAK          = "Cloaks",
    INVTYPE_WEAPON         = "One-Hand",
    INVTYPE_SHIELD         = "Shields",
    INVTYPE_2HWEAPON       = "Two-Hand",
    INVTYPE_WEAPONMAINHAND = "Main Hand",
    INVTYPE_WEAPONOFFHAND  = "Off Hand",
    INVTYPE_HOLDABLE       = "Held In Off-hand",
    INVTYPE_RANGED         = "Ranged",
    INVTYPE_THROWN         = "Thrown",
    INVTYPE_RANGEDRIGHT    = "Ranged",
    INVTYPE_RELIC          = "Relics",
    INVTYPE_TABARD         = "Tabards",
    INVTYPE_BAG            = "Bags",
};

local CATEGORY_ORDER = {
    "ALL",
    "Head",
    "Neck",
    "Shoulders",
    "Cloaks",
    "Chest",
    "Shirt",
    "Tabards",
    "Wrists",
    "Hands",
    "Waist",
    "Legs",
    "Boots",
    "Rings",
    "Trinkets",
    "One-Hand",
    "Two-Hand",
    "Main Hand",
    "Off Hand",
    "Held In Off-hand",
    "Shields",
    "Ranged",
    "Thrown",
    "Relics",
    "Bags",
    "Other",
};

local CATEGORY_SORT_INDEX = {};
for index, label in ipairs(CATEGORY_ORDER) do
    CATEGORY_SORT_INDEX[label] = index;
end

local function SplitString(str, delim)
    local r = {};
    if not str or str == "" then return r; end
    delim = delim or "\t";

    local start = 1;
    while true do
        local pos = string.find(str, delim, start, true);
        if not pos then
            table.insert(r, string.sub(str, start));
            break
        end

        table.insert(r, string.sub(str, start, pos - 1));
        start = pos + string.len(delim);
    end
    return r;
end

local function SplitEscaped(str, delim)
    local result = {};
    if not str or str == "" then
        if str == "" then
            table.insert(result, "");
        end
        return result;
    end

    local current = "";
    local escaped = false;
    for i = 1, string.len(str) do
        local ch = string.sub(str, i, i);
        if escaped then
            current = current .. "\\" .. ch;
            escaped = false;
        elseif ch == "\\" then
            escaped = true;
        elseif ch == delim then
            table.insert(result, current);
            current = "";
        else
            current = current .. ch;
        end
    end
    if escaped then
        current = current .. "\\";
    end
    table.insert(result, current);
    return result;
end

local function UnescapeField(text)
    if not text then
        return "";
    end

    local result = "";
    local escaped = false;
    for i = 1, string.len(text) do
        local ch = string.sub(text, i, i);
        if escaped then
            if ch == "n" then
                result = result .. "\n";
            elseif ch == "r" then
                result = result .. "\r";
            elseif ch == "t" then
                result = result .. "\t";
            else
                result = result .. ch;
            end
            escaped = false;
        elseif ch == "\\" then
            escaped = true;
        else
            result = result .. ch;
        end
    end
    if escaped then
        result = result .. "\\";
    end
    return result;
end

local function Mod(value, divisor)
    return value - math.floor(value / divisor) * divisor;
end

local function FormatMoney(copper)
    local g = math.floor(copper / 10000);
    local s = math.floor(Mod(copper, 10000) / 100);
    local c = Mod(copper, 100);
    if g > 0 then
        return g .. "g " .. s .. "s " .. c .. "c";
    elseif s > 0 then
        return s .. "s " .. c .. "c";
    else
        return c .. "c";
    end
end

local function GetRecipeCategory(rec)
    if not rec then
        return "";
    end

    if rec.category and rec.category ~= "" then
        return rec.category;
    end

    if not GetItemInfo then
        return "";
    end

    -- Turtle/Vanilla returns: name, link, quality, minLevel, type,
    -- subType, stackSize, equipLoc, texture.
    local _, _, _, _, itemType, subClass, _, equipLoc = GetItemInfo("item:" .. rec.itemId);
    rec.itemType = itemType or "Other";
    if not rec.subClass or rec.subClass == "" then
        rec.subClass = subClass or "Other";
    end

    if equipLoc and EQUIP_CATEGORY_LABELS[equipLoc] then
        return EQUIP_CATEGORY_LABELS[equipLoc];
    end
    if rec.subClass == "Shields" then
        return "Shields";
    end

    return "";
end

local function GetTargetTexture(rec)
    if not rec or rec.targetBag == nil or rec.targetSlot == nil then
        return nil;
    end

    if rec.targetBag == 255 then
        if rec.targetSlot >= 0 and rec.targetSlot < 19 and GetInventoryItemTexture then
            return GetInventoryItemTexture("player", rec.targetSlot + 1);
        end

        if rec.targetSlot >= 23 and rec.targetSlot <= 38 and GetContainerItemInfo then
            local texture = GetContainerItemInfo(0, rec.targetSlot - 22);
            return texture;
        end
    end

    if rec.targetBag and rec.targetBag >= 19 and rec.targetBag <= 22 and GetContainerItemInfo then
        local apiBag = rec.targetBag - 18;
        local texture = GetContainerItemInfo(apiBag, rec.targetSlot + 1);
        return texture;
    end

    return nil;
end

local function GetEnchantTargetLink(rec)
    if not rec or rec.targetBag == nil or rec.targetSlot == nil then
        return nil;
    end

    if rec.targetBag == 255 then
        if rec.targetSlot >= 0 and rec.targetSlot < 19 and GetInventoryItemLink then
            return GetInventoryItemLink("player", rec.targetSlot + 1);
        end

        if rec.targetSlot >= 23 and rec.targetSlot <= 38 and GetContainerItemLink then
            return GetContainerItemLink(0, rec.targetSlot - 22);
        end
    end

    if rec.targetBag and rec.targetBag >= 19 and rec.targetBag <= 22 and GetContainerItemLink then
        local apiBag = rec.targetBag - 18;
        return GetContainerItemLink(apiBag, rec.targetSlot + 1);
    end

    return nil;
end

local ItemTextureCache = {};

local function GetCraftingOrderItemTexture(itemId)
    if not itemId or itemId <= 0 then
        return nil;
    end

    if ItemTextureCache[itemId] then
        return ItemTextureCache[itemId];
    end

    if GetItemInfo then
        -- The stock ItemButtonTemplate is populated through the ninth
        -- GetItemInfo return (the IconTexture child is set by
        -- SetItemButtonTexture).
        local _, _, _, _, _, _, _, _, texture = GetItemInfo("item:" .. itemId);
        if texture then
            ItemTextureCache[itemId] = texture;
            return texture;
        end
    end

    return nil;
end

local function SetCraftingOrderItemTexture(button, texture)
    if not button then
        return;
    end

    if SetItemButtonTexture then
        SetItemButtonTexture(button, texture);
    else
        local iconTexture = _G[button:GetName() .. "IconTexture"];
        if iconTexture then
            iconTexture:SetTexture(texture);
        end
    end
end

local function GetRecipeIcon(rec)
    if not rec then
        return "Interface\\Icons\\INV_Misc_QuestionMark";
    end

    local targetTexture = GetTargetTexture(rec);
    if targetTexture then
        return targetTexture;
    end

    if rec.itemId and rec.itemId > 0 then
        local itemIcon = GetCraftingOrderItemTexture(rec.itemId);
        if itemIcon then
            return itemIcon;
        end
    end

    if rec.spellId and rec.spellId > 0 and GetSpellInfo then
        local _, _, spellIcon = GetSpellInfo(rec.spellId);
        if spellIcon then
            return spellIcon;
        end
    end

    return "Interface\\Icons\\INV_Misc_QuestionMark";
end

local function RebuildRecipeCategories()
    local seenCategories = {};
    local seenSubclasses = {};
    local categories = { "ALL" };
    local subclasses = { "ALL" };

    for _, rec in ipairs(State.recipes) do
        rec.category = GetRecipeCategory(rec);
        if rec.category ~= "" and not seenCategories[rec.category] then
            seenCategories[rec.category] = true;
            table.insert(categories, rec.category);
        end
        if not seenSubclasses[rec.subClass] then
            seenSubclasses[rec.subClass] = true;
            table.insert(subclasses, rec.subClass);
        end
    end

    table.sort(categories, function(a, b)
        if a == b then return false; end
        local ia = CATEGORY_SORT_INDEX[a] or 999;
        local ib = CATEGORY_SORT_INDEX[b] or 999;
        if ia == ib then
            return a < b;
        end
        return ia < ib;
    end);
    table.sort(subclasses, function(a, b)
        if a == b then return false; end
        if a == "ALL" then return true; end
        if b == "ALL" then return false; end
        return a < b;
    end);

    State.categories = categories;
    State.subclasses = subclasses;

    if State.categoryFilter ~= "ALL" and not seenCategories[State.categoryFilter] then
        State.categoryFilter = "ALL";
    end
    if State.subclassFilter ~= "ALL" and not seenSubclasses[State.subclassFilter] then
        State.subclassFilter = "ALL";
    end

    if CraftingOrderCategoryDropDown then
        UIDropDownMenu_Initialize(CraftingOrderCategoryDropDown, CraftingOrderCategoryDropDown_Initialize);
        UIDropDownMenu_SetText(State.categoryFilter == "ALL" and (ALL_INVENTORY_SLOTS or ALL) or State.categoryFilter, CraftingOrderCategoryDropDown);
    end
    if CraftingOrderSubClassDropDown then
        UIDropDownMenu_Initialize(CraftingOrderSubClassDropDown, CraftingOrderSubClassDropDown_Initialize);
        UIDropDownMenu_SetText(State.subclassFilter == "ALL" and (ALL_SUBCLASSES or ALL) or State.subclassFilter, CraftingOrderSubClassDropDown);
    end
end

local function BuildEnchantSlotRecipes(slotData)
    local recipes = {};
    if not slotData or slotData == "" then return recipes; end

    for _, entry in ipairs(SplitString(slotData, ";")) do
        local fields = SplitString(entry, ",");
        local slotId = tonumber(fields[1]) or 0;
        local slotName = fields[2] or "Unknown Slot";
        local itemLink = fields[3] or "";
        local itemName = fields[4] or "";
        local itemId = 0;
        if itemLink ~= "" then
            local _, _, itemIdText = string.find(itemLink, "item:(%d+)");
            itemId = tonumber(itemIdText) or 0;
        end

        table.insert(recipes, {
            spellId = 0,
            itemId = itemId,
            itemName = itemName ~= "" and (slotName .. " - " .. itemName) or (slotName .. " - (empty)"),
            tier = 1,
            reqRank = 0,
            goldFee = 0,
            numMade = 1,
            numAvailable = itemName ~= "" and 1 or 0,
            subClass = "Equipment",
            category = "Equipment",
            materials = {},
            isEnchantSlot = true,
            slotId = slotId,
            slotName = slotName,
            itemLink = itemLink,
        });
    end

    return recipes;
end

local function CloneMaterials(materials)
    local cloned = {};
    if not materials then
        return cloned;
    end

    for _, mat in ipairs(materials) do
        table.insert(cloned, {
            itemId = mat.itemId,
            count = mat.count,
            name = mat.name,
            playerCount = mat.playerCount,
        });
    end

    return cloned;
end

local function FindRecipeBySpellId(recipes, spellId)
    if not recipes then
        return nil;
    end

    for _, rec in ipairs(recipes) do
        if rec.spellId == spellId then
            return rec;
        end
    end

    return nil;
end

local function BuildEnchantTargetRecipes(dataStr, baseRecipe)
    local recipes = {};
    local parts = SplitEscaped(dataStr, "|");
    local spellId = tonumber(parts[1]) or 0;
    local materials = CloneMaterials(baseRecipe and baseRecipe.materials or nil);
    local goldFee = baseRecipe and (baseRecipe.goldFee or 0) or 0;
    local reqRank = baseRecipe and (baseRecipe.reqRank or 0) or 0;
    local tier = baseRecipe and (baseRecipe.tier or 1) or 1;
    local subClass = baseRecipe and (baseRecipe.subClass or "Equipment") or "Equipment";
    local category = baseRecipe and (baseRecipe.category or "Equipment") or "Equipment";
    local cooldownEndsAt = baseRecipe and baseRecipe.cooldownEndsAt or nil;

    for i = 2, table.getn(parts) do
        local fields = SplitEscaped(parts[i], "^");
        local bagId = tonumber(fields[1]) or 0;
        local slotId = tonumber(fields[2]) or 0;
        local locationName = UnescapeField(fields[3] or "Unknown");
        local itemId = tonumber(fields[4]) or 0;
        local itemName = UnescapeField(fields[5] or "Unknown Item");

        table.insert(recipes, {
            spellId = spellId,
            itemId = itemId,
            itemName = locationName .. " - " .. itemName,
            rawItemName = itemName,
            tier = tier,
            reqRank = reqRank,
            goldFee = goldFee,
            numMade = 1,
            numAvailable = 1,
            subClass = subClass,
            category = category,
            cooldownEndsAt = cooldownEndsAt,
            materials = CloneMaterials(materials),
            isEnchantTarget = true,
            targetBag = bagId,
            targetSlot = slotId,
            targetName = locationName,
        });
    end

    return spellId, recipes;
end

local function UpdateCancelButton()
    if not CraftingOrderCancelButton then
        return;
    end

    if State.selectingEnchantTarget then
        CraftingOrderCancelButton:SetText(BACK or "Back");
    else
        CraftingOrderCancelButton:SetText(CANCEL or "Exit");
    end
end

local SendToServer;
local RequestCraftingListPage;
local PendingEnchantSpellId;
local PendingEnchantName;
local PendingEnchantBag;
local PendingEnchantSlot;
local CraftingOrderFrameCloseInProgress = false;

local function RegisterCraftingOrderEscapeFrame()
    if not UISpecialFrames then
        return;
    end

    local frameName = "CraftingOrderFrame";
    for i = 1, table.getn(UISpecialFrames) do
        if UISpecialFrames[i] == frameName then
            return;
        end
    end
    table.insert(UISpecialFrames, frameName);
end

-- Register before the XML frame's OnLoad runs.  Turtle's legacy UI load order
-- can otherwise leave a frame loaded without its ESC registration.
RegisterCraftingOrderEscapeFrame();

local function ResetUiModes()
    State.isEnchantMode = false;
    State.isDisenchantMode = false;
    State.isMillingMode = false;
    State.isProspectingMode = false;
    State.isHandInMode = false;
    State.selectingEnchantTarget = false;
    State.selectedEnchantSpellId = nil;
    State.selectedEnchantSpellName = nil;
    State.baseEnchantRecipes = nil;
end

local function RefreshCurrentView()
    State.selectingEnchantTarget = false;
    State.selectedEnchantSpellId = nil;
    State.selectedEnchantSpellName = nil;
    State.baseEnchantRecipes = nil;
    RequestCraftingListPage(0);
end

local function HideCraftingOrderFrame()
    ResetUiModes();
    if CraftingOrderFrame and CraftingOrderFrame:IsVisible() then
        CraftingOrderFrame:Hide();
    end
end

local function CloseCraftingOrderFrame()
    -- Hide first so a failed/unavailable addon-message channel cannot prevent
    -- the local window from closing.
    CraftingOrderFrameCloseInProgress = true;
    HideCraftingOrderFrame();
    CraftingOrderFrameCloseInProgress = false;
    SendToServer("CLOSE");
end

function CraftingOrderFrame_OnHide()
    ResetUiModes();
    -- ESC through UISpecialFrames calls frame:Hide() directly, so notify the
    -- server from OnHide as well as from the explicit close controls.
    if not CraftingOrderFrameCloseInProgress then
        SendToServer("CLOSE");
    end
end

local function ResetRecipeListPosition()
    FauxScrollFrame_SetOffset(CraftingOrderListScrollFrame, 0);
    CraftingOrderListScrollFrameScrollBar:SetValue(0);
end

local function UpdateCraftingOrderListHeight(numRows)
    if not CraftingOrderListScrollFrame then
        return;
    end

    -- Keep the full viewport for lists that need scrolling.  When the
    -- filtered list fits, remove the unused row-sized space at the bottom.
    local visibleRows = math.min(numRows or 0, TRADE_SKILLS_DISPLAYED);
    local listHeight = 152;
    if numRows and numRows > 0 and numRows <= TRADE_SKILLS_DISPLAYED then
        listHeight = (visibleRows * TRADE_SKILL_HEIGHT) + 8;
    end
    CraftingOrderListScrollFrame:SetHeight(listHeight);
end

local function SelectFirstFilteredRecipe()
    local filteredRows = GetFilteredRows();
    for i, row in ipairs(filteredRows) do
        if row.recipe then
            CraftingOrderFrame_SetSelection(i);
            return;
        end
    end

    State.selectedRecipeIndex = 0;
    CraftingOrderFrame_SetSelection(0);
end

SendToServer = function(msg)
    if not SendAddonMessage then
        return;
    end

    local tabPos = string.find(msg or "", "\t", 1, true);
    local opcode = tabPos and string.sub(msg, 1, tabPos - 1) or (msg or "");
    local payload = tabPos and string.sub(msg, tabPos + 1) or "";

    CraftingOrderRequestId = CraftingOrderRequestId + 1;
    if CraftingOrderRequestId > 4294967294 then
        CraftingOrderRequestId = 1;
    end

    local body = tostring(CRAFTING_ORDER_PROTOCOL_VERSION) .. "\t" ..
        tostring(CraftingOrderRequestId) .. "\t" .. opcode;
    if payload ~= "" then
        body = body .. "\t" .. payload;
    end

    -- The Tortoise packet hook consumes addon messages sent through the
    -- guild path before normal guild chat routing.
    SendAddonMessage("CO", body, "GUILD");
end

RequestCraftingListPage = function(page)
    page = tonumber(page) or 0;
    if State.isHandInMode then
        SendToServer("REQUEST_HANDIN\t" .. page);
    elseif State.isDisenchantMode then
        SendToServer("REQUEST_DISENCHANT_ITEMS\t" .. page);
    else
        SendToServer("REQUEST_RECIPES\t\t0\t" .. page);
    end
end

if StaticPopupDialogs and not StaticPopupDialogs["CRAFTING_ORDER_ENCHANT_CONFIRM"] then
    StaticPopupDialogs["CRAFTING_ORDER_ENCHANT_CONFIRM"] = {
        text = "Apply %s?",
        button1 = ACCEPT or "Accept",
        button2 = CANCEL or "Cancel",
        OnAccept = function()
            if PendingEnchantSpellId then
                if PendingEnchantSlot then
                    SendToServer("ENCHANT\t" .. PendingEnchantSpellId .. "\t" .. PendingEnchantBag .. "\t" .. PendingEnchantSlot);
                else
                    SendToServer("ENCHANT\t" .. PendingEnchantSpellId);
                end
            end
            PendingEnchantSpellId = nil;
            PendingEnchantName = nil;
            PendingEnchantBag = nil;
            PendingEnchantSlot = nil;
        end,
        OnCancel = function()
            PendingEnchantSpellId = nil;
            PendingEnchantName = nil;
            PendingEnchantBag = nil;
            PendingEnchantSlot = nil;
        end,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    };
end

local function ShowEnchantConfirm(spellId, enchantName, bagId, slotId, slotName, itemName)
    PendingEnchantSpellId = spellId;
    PendingEnchantName = enchantName;
    PendingEnchantBag = bagId;
    PendingEnchantSlot = slotId;

    local text = enchantName or "this enchant";
    if slotName and itemName then
        text = text .. " to " .. slotName .. " (" .. itemName .. ")";
    else
        text = text .. " to your equipped item";
    end

    if StaticPopup_Show then
        StaticPopup_Show("CRAFTING_ORDER_ENCHANT_CONFIRM", text);
    else
        if PendingEnchantSlot then
            SendToServer("ENCHANT\t" .. spellId .. "\t" .. PendingEnchantBag .. "\t" .. PendingEnchantSlot);
        else
            SendToServer("ENCHANT\t" .. spellId);
        end
        PendingEnchantSpellId = nil;
        PendingEnchantName = nil;
        PendingEnchantBag = nil;
        PendingEnchantSlot = nil;
    end
end

local function BuildServerMessage(prefix, msg)
    if not SERVER_PREFIXES[prefix] then
        return nil;
    end

    if msg and msg ~= "" then
        return prefix .. "\t" .. msg;
    end

    return prefix;
end

-----------------------------------------------------------------------
-- Addon message parsing
-----------------------------------------------------------------------

local function BuildRecipesFromString(dataStr)
    -- Recipe record:
    -- spell/guid,item,name,tier,required skill,fee,output count,available,
    -- subclass,inventory slot,target bag,target slot,cooldown.
    local recipes = {};
    if not dataStr or dataStr == "" then
        return recipes;
    end

    for _, entry in ipairs(SplitEscaped(dataStr, "|")) do
        local sections = SplitEscaped(entry, ";");
        local hdr = SplitEscaped(sections[1] or "", ",");
        if table.getn(hdr) >= 8 then
            local isDisenchant = State.isDisenchantMode;
            local rec = {
                spellId = tonumber(hdr[1]) or 0,
                itemId = tonumber(hdr[2]) or 0,
                itemName = UnescapeField(hdr[3] or ""),
                tier = tonumber(hdr[4]) or 1,
                reqRank = tonumber(hdr[5]) or 0,
                goldFee = tonumber(hdr[6]) or 0,
                numMade = tonumber(hdr[7]) or 1,
                numAvailable = tonumber(hdr[8]) or 0,
                subClass = UnescapeField(hdr[9] or ""),
                category = UnescapeField(hdr[10] or ""),
                materials = {},
            };

            if isDisenchant then
                rec.targetBag = tonumber(hdr[12]);
                rec.targetSlot = tonumber(hdr[13]);
            else
                rec.cooldownRemaining = tonumber(hdr[13]) or 0;
                rec.targetBag = tonumber(hdr[11]);
                rec.targetSlot = tonumber(hdr[12]);
                if rec.cooldownRemaining > 0 then
                    rec.cooldownEndsAt = GetTime() + rec.cooldownRemaining;
                end
            end

            if rec.subClass == "" then
                rec.subClass = "Other";
            end

            for j = 2, table.getn(sections) do
                local mf = SplitEscaped(sections[j], ",");
                if table.getn(mf) >= 5 then
                    table.insert(rec.materials, {
                        itemId = tonumber(mf[1]) or 0,
                        count = tonumber(mf[2]) or 0,
                        name = UnescapeField(mf[3] or ""),
                        playerCount = tonumber(mf[5]) or 0,
                    });
                end
            end
            table.insert(recipes, rec);
        end
    end
    return recipes;
end

local function BuildHandInRecords(dataStr)
    local recipes = {};
    if not dataStr or dataStr == "" then
        return recipes;
    end

    for _, entry in ipairs(SplitEscaped(dataStr, "|")) do
        local fields = SplitEscaped(entry, ",");
        local guid = tonumber(fields[1]) or 0;
        if guid > 0 then
            table.insert(recipes, {
                itemGuid = guid,
                spellId = guid,
                itemId = tonumber(fields[2]) or 0,
                itemName = UnescapeField(fields[3] or ""),
                taughtSpell = tonumber(fields[4]) or 0,
                createdItemId = tonumber(fields[5]) or 0,
                tier = 1,
                reqRank = 0,
                goldFee = 0,
                numMade = 1,
                numAvailable = 1,
                subClass = "Recipes",
                category = "Recipes",
                materials = {},
                isHandIn = true,
            });
        end
    end
    return recipes;
end

-----------------------------------------------------------------------
-- Exposed helper for XML templates to look up recipes by their
-- position in the currently-filtered list.
-----------------------------------------------------------------------

function CraftingOrder_GetRecipeByFilteredIndex(index)
    local filtered = GetFilteredRows();
    return filtered[index] and filtered[index].recipe;
end

function CraftingOrder_GetRowByFilteredIndex(index)
    local filtered = GetFilteredRows();
    return filtered[index];
end

GetFilteredRecipes = function()
    local filtered = {};
    local searchLower = string.lower(State.filterText);
    for _, rec in ipairs(State.recipes) do
        if State.onlyMakeable and rec.numAvailable <= 0 then
            -- skip
        elseif State.filterText ~= "" and not string.find(string.lower(rec.itemName or ""), searchLower, 1, true) then
            -- skip
        elseif State.subclassFilter ~= "ALL" and rec.subClass ~= State.subclassFilter then
            -- skip
        elseif State.categoryFilter ~= "ALL" and rec.category ~= State.categoryFilter then
            -- skip
        else
            table.insert(filtered, rec);
        end
    end
    table.sort(filtered, function(a, b)
        if a.subClass ~= b.subClass then
            return a.subClass < b.subClass;
        end
        local ca = CATEGORY_SORT_INDEX[a.category] or 999;
        local cb = CATEGORY_SORT_INDEX[b.category] or 999;
        if ca ~= cb then
            return ca < cb;
        end
        return a.itemName < b.itemName;
    end);
    return filtered;
end

GetFilteredRows = function()
    local rows = {};
    local lastHeader = nil;

    for _, rec in ipairs(GetFilteredRecipes()) do
        local header = rec.subClass or "Other";
        if header ~= lastHeader then
            table.insert(rows, { header = header, collapsed = State.collapsedHeaders[header] });
            lastHeader = header;
        end
        if not State.collapsedHeaders[header] then
            table.insert(rows, { recipe = rec });
        end
    end

    return rows;
end

-----------------------------------------------------------------------
-- Recipe list update
-----------------------------------------------------------------------

function CraftingOrderFrame_Update()
    local filteredRows = GetFilteredRows();
    local numRows = table.getn(filteredRows);
    UpdateCraftingOrderListHeight(numRows);
    local skillOffset = FauxScrollFrame_GetOffset(CraftingOrderListScrollFrame);
    local selectedRow = nil;

    if (numRows == 0) then
        State.selectedRecipeIndex = 0;
        CraftingOrderSkillName:Hide();
        CraftingOrderSkillIcon:Hide();
        CraftingOrderRequirementLabel:Hide();
        CraftingOrderRequirementText:SetText("");
        CraftingOrderDescription:SetText(" ");
        CraftingOrderReagentLabel:Hide();
        CraftingOrderGoldCost:SetText("");
        CraftingOrderGoldCost:Hide();
        for i = 1, MAX_TRADE_SKILL_REAGENTS do
            _G["CraftingOrderReagent" .. i]:Hide();
        end
        CraftingOrderCreateButton:Disable();
        CraftingOrderCreateAllButton:Disable();
        CraftingOrderHighlightFrame:Hide();
        -- Still update scroll frame UI
        FauxScrollFrame_Update(CraftingOrderListScrollFrame, 0, TRADE_SKILLS_DISPLAYED, TRADE_SKILL_HEIGHT, nil, nil, nil, CraftingOrderHighlightFrame, 293, 316);
        -- Hide all skill buttons
        for i = 1, TRADE_SKILLS_DISPLAYED do
            _G["CraftingOrderSkill" .. i]:Hide();
        end
        return;
    end

    -- Always show the edit box for our custom UI
    CraftingOrderFrameEditBox:Show();

    -- ScrollFrame update
    FauxScrollFrame_Update(CraftingOrderListScrollFrame, numRows, TRADE_SKILLS_DISPLAYED, TRADE_SKILL_HEIGHT, nil, nil, nil, CraftingOrderHighlightFrame, 293, 316);

    CraftingOrderHighlightFrame:Hide();

    if State.selectedRecipeIndex > 0 and State.selectedRecipeIndex <= numRows then
        selectedRow = filteredRows[State.selectedRecipeIndex];
        if not selectedRow or not selectedRow.recipe then
            State.selectedRecipeIndex = 0;
            selectedRow = nil;
        end
    else
        State.selectedRecipeIndex = 0;
    end

    local skillButton, skillButtonCount;

    for i = 1, TRADE_SKILLS_DISPLAYED do
                local skillIndex = i + skillOffset;
        skillButton      = _G["CraftingOrderSkill" .. i];
        skillButtonCount = _G["CraftingOrderSkill" .. i .. "Count"];

        if skillIndex <= numRows then
            local row = filteredRows[skillIndex];
            local rec = row.recipe;

            -- Set button width based on scrollbar visibility
            if CraftingOrderListScrollFrame:IsVisible() then
                skillButton:SetWidth(293);
            else
                skillButton:SetWidth(323);
            end

            local color = row.header and CraftingOrderTypeColor["header"] or CraftingOrderTypeColor["optimal"];
            if color then
                skillButton:SetTextColor(color.r, color.g, color.b);
                if skillButtonCount then
                    skillButtonCount:SetTextColor(color.r, color.g, color.b);
                end
                skillButton.r = color.r;
                skillButton.g = color.g;
                skillButton.b = color.b;
            end

            skillButton:SetID(skillIndex);
            skillButton:Show();

            if row.header then
                if row.collapsed then
                    skillButton:SetNormalTexture("Interface\\Buttons\\UI-PlusButton-Up");
                else
                    skillButton:SetNormalTexture("Interface\\Buttons\\UI-MinusButton-Up");
                end
                _G["CraftingOrderSkill" .. i .. "Highlight"]:SetTexture("Interface\\Buttons\\UI-PlusButton-Hilight");
                skillButton:SetText(row.header);
                skillButtonCount:SetText("");
            elseif rec.numAvailable <= 0 then
                skillButton:SetNormalTexture("");
                _G["CraftingOrderSkill" .. i .. "Highlight"]:SetTexture("");
                skillButton:SetText("  " .. rec.itemName);
                skillButtonCount:SetText("");
            else
                skillButton:SetNormalTexture("");
                _G["CraftingOrderSkill" .. i .. "Highlight"]:SetTexture("");
                skillButton:SetText("  " .. rec.itemName);
                skillButtonCount:SetText("[" .. rec.numAvailable .. "]");
            end

            -- Highlight selected recipe
            if rec and State.selectedRecipeIndex == skillIndex then
                CraftingOrderHighlightFrame:SetPoint("TOPLEFT", "CraftingOrderSkill" .. i, "TOPLEFT", 0, 0);
                CraftingOrderHighlightFrame:Show();
                if skillButtonCount then
                    skillButtonCount:SetTextColor(HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b);
                end
                skillButton:LockHighlight();
                skillButton.isHighlighted = true;
            else
                skillButton:UnlockHighlight();
                skillButton.isHighlighted = false;
            end
        else
            skillButton:Hide();
        end
    end

    -- Track numAvailable for the selected recipe for Create All
    if selectedRow and selectedRow.recipe then
        CraftingOrderFrame.numAvailable = math.abs(selectedRow.recipe.numAvailable);
    else
        CraftingOrderFrame.numAvailable = 0;
    end
end

-----------------------------------------------------------------------
-- Recipe selection (detail panel)
-----------------------------------------------------------------------

local function GetRecipeCooldownRemaining(rec)
    if not rec or not rec.cooldownEndsAt then
        return 0;
    end

    return math.max(0, math.ceil(rec.cooldownEndsAt - GetTime()));
end

local function FormatCooldownRemaining(totalSeconds)
    local days = math.floor(totalSeconds / 86400);
    local hours = math.floor(Mod(totalSeconds, 86400) / 3600);
    local minutes = math.floor(Mod(totalSeconds, 3600) / 60);
    local seconds = Mod(totalSeconds, 60);

    if days > 0 then
        return string.format("%dd %dh %dm", days, hours, minutes);
    elseif hours > 0 then
        return string.format("%dh %dm %ds", hours, minutes, seconds);
    elseif minutes > 0 then
        return string.format("%dm %ds", minutes, seconds);
    end

    return string.format("%ds", seconds);
end

local function UpdateRecipeCooldownDisplay(rec)
    local remaining = GetRecipeCooldownRemaining(rec);
    if remaining > 0 then
        CraftingOrderSkillCooldown:SetText("Cooldown remaining: " .. FormatCooldownRemaining(remaining));
        return true;
    end

    if rec then
        rec.cooldownEndsAt = nil;
        rec.cooldownRemaining = 0;
    end
    CraftingOrderSkillCooldown:SetText("");
    return false;
end

local function UpdateCraftingOrderDetailScroll(numReagents)
    local scrollFrame = CraftingOrderDetailScrollFrame;
    local child = CraftingOrderDetailScrollChildFrame;
    if not scrollFrame or not child then
        return;
    end

    local viewportHeight = scrollFrame:GetHeight();
    local contentHeight = (numReagents and numReagents > 4) and 185 or viewportHeight;
    child:SetHeight(contentHeight);
    if scrollFrame.UpdateScrollChildRect then
        scrollFrame:UpdateScrollChildRect();
    end

    local scrollBar = _G["CraftingOrderDetailScrollFrameScrollBar"];
    if scrollBar then
        if contentHeight > viewportHeight then
            scrollBar:Show();
        else
            scrollBar:Hide();
            scrollFrame:SetVerticalScroll(0);
        end
    end
end

local function CraftingOrderFrame_SetSelectionBase(index)
    if index == 0 then
        CraftingOrderFrame.cooldownRecipe = nil;
        CraftingOrderHighlightFrame:Hide();
        CraftingOrderSkillName:Hide();
        CraftingOrderSkillIcon:Hide();
        CraftingOrderRequirementLabel:Hide();
        CraftingOrderRequirementText:SetText("");
        CraftingOrderSkillCooldown:SetText("");
        CraftingOrderDescription:SetText(" ");
        CraftingOrderReagentLabel:Hide();
        CraftingOrderGoldCost:SetText("");
        CraftingOrderGoldCost:Hide();
        UpdateCraftingOrderDetailScroll(0);
        for i = 1, MAX_TRADE_SKILL_REAGENTS do
            _G["CraftingOrderReagent" .. i]:Hide();
        end
        CraftingOrderCreateButton:Disable();
        CraftingOrderCreateAllButton:Disable();
        return;
    end

    local filteredRows = GetFilteredRows();
    if index > table.getn(filteredRows) then
        CraftingOrderHighlightFrame:Hide();
        return;
    end

    local row = filteredRows[index];
    local rec = row and row.recipe;
    if not rec then
        CraftingOrderHighlightFrame:Hide();
        return;
    end

    State.selectedRecipeIndex = index;

    local color = CraftingOrderTypeColor["optimal"];
    if color then
        CraftingOrderHighlight:SetVertexColor(color.r, color.g, color.b);
    end

    -- Title & rank bar
    CraftingOrderFrameTitleText:SetText("Crafting Orders");
    CraftingOrderRankFrame:SetStatusBarColor(0.0, 0.0, 1.0, 0.5);
    CraftingOrderRankFrameBackground:SetVertexColor(0.0, 0.0, 0.75, 0.5);
    CraftingOrderRankFrame:SetMinMaxValues(0, 1);
    CraftingOrderRankFrame:SetValue(1);
    CraftingOrderRankFrameSkillRank:SetText(State.professionName);

    CraftingOrderSkillName:Show();
    CraftingOrderSkillName:SetText(rec.itemName);

    local onCooldown = UpdateRecipeCooldownDisplay(rec);
    CraftingOrderFrame.cooldownRecipe = onCooldown and rec or nil;

    -- Icon
    CraftingOrderSkillIcon:Show();
    CraftingOrderSkillIcon:SetNormalTexture(GetRecipeIcon(rec));

    -- Item count on icon
    if rec.numMade > 1 then
        CraftingOrderSkillIconCount:SetText(rec.numMade);
        if CraftingOrderSkillIconCount:GetWidth() > 39 then
            CraftingOrderSkillIconCount:SetText("~" .. math.floor(rec.numMade));
        end
    else
        CraftingOrderSkillIconCount:SetText("");
    end

    -- Reagents
    local numReagents = table.getn(rec.materials);
    UpdateCraftingOrderDetailScroll(numReagents);
    if numReagents > 0 then
        CraftingOrderReagentLabel:Show();
    else
        CraftingOrderReagentLabel:Hide();
    end

    local creatable = not onCooldown;
    for i = 1, numReagents do
        local mat = rec.materials[i];
        local reagent = _G["CraftingOrderReagent" .. i];
        local name    = _G["CraftingOrderReagent" .. i .. "Name"];
        local count   = _G["CraftingOrderReagent" .. i .. "Count"];

        if not reagent then break end

        SetCraftingOrderItemTexture(reagent, GetCraftingOrderItemTexture(mat.itemId) or "Interface\\Icons\\INV_Misc_QuestionMark");
        reagent.itemLink = "item:" .. mat.itemId .. ":0:0:0:0:0:0:0";
        reagent:Show();

        if name then
            name:SetText(mat.name);
            if mat.playerCount < mat.count then
                SetItemButtonTextureVertexColor(reagent, 0.5, 0.5, 0.5);
                name:SetTextColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b);
                creatable = false;
            else
                SetItemButtonTextureVertexColor(reagent, 1.0, 1.0, 1.0);
                name:SetTextColor(HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b);
            end
        end
        if count then
            local pc = mat.playerCount;
            if pc >= 100 then pc = "*"; end
            count:SetText(pc .. " /" .. mat.count);
        end
    end

    for i = numReagents + 1, MAX_TRADE_SKILL_REAGENTS do
        _G["CraftingOrderReagent" .. i]:Hide();
    end

    -- Fee in the detail header. Required-skill text is intentionally not
    -- shown for crafting or service records; a zero fee is explicitly Free.
    CraftingOrderRequirementLabel:SetText("Fee:");
    CraftingOrderRequirementLabel:Show();
    if rec.goldFee and rec.goldFee > 0 then
        CraftingOrderRequirementText:SetText(FormatMoney(rec.goldFee));
    else
        CraftingOrderRequirementText:SetText("Free");
    end

    -- Create button state
    if creatable then
        CraftingOrderCreateButton:Enable();
        CraftingOrderCreateAllButton:Enable();
    else
        CraftingOrderCreateButton:Disable();
        CraftingOrderCreateAllButton:Disable();
    end

    -- Description
    CraftingOrderDescription:SetText(" ");
    CraftingOrderReagentLabel:SetPoint("TOPLEFT", "CraftingOrderDescription", "TOPLEFT", 0, 0);

    -- Keep the legacy lower fee line empty and hidden; the header above is
    -- the single fee display.
    CraftingOrderGoldCost:SetText("");
    CraftingOrderGoldCost:Hide();

    -- Reset input box to 1
    CraftingOrderInputBox:SetNumber(1);

    -- Always show Create / CreateAll buttons (this is our custom UI, not linked/inspect)
    CraftingOrderCreateAllButton:Show();
    CraftingOrderDecrementButton:Show();
    CraftingOrderInputBox:Show();
    CraftingOrderIncrementButton:Show();
    CraftingOrderCreateButton:SetText(CREATE);
    CraftingOrderCreateButton:Show();

    CraftingOrderFrameBottomLeftTexture:SetTexture("Interface\\TradeSkillFrame\\UI-TradeSkill-BotLeft");
    CraftingOrderFrameBottomRightTexture:SetTexture("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotRight");

    -- No link button for custom UI
end

-----------------------------------------------------------------------
-- Skill button click
-----------------------------------------------------------------------

local function CraftingOrderSkillButton_OnClickBase(self, button)
    if button == "LeftButton" then
        local row = CraftingOrder_GetRowByFilteredIndex(self:GetID());
        if row and row.header then
            State.collapsedHeaders[row.header] = not State.collapsedHeaders[row.header];
            State.selectedRecipeIndex = 0;
            CraftingOrderFrame_SetSelection(0);
            CraftingOrderFrame_Update();
            return;
        end
        if not row or not row.recipe then
            return;
        end
        CraftingOrderFrame_SetSelection(self:GetID());
        CraftingOrderFrame_Update();
    end
end

-----------------------------------------------------------------------
-- Filter / search
-----------------------------------------------------------------------

function CraftingOrderFilter_OnTextChanged(self)
    self = self or this or CraftingOrderFrameEditBox;
    local text = self:GetText();

    if text == SEARCH then
        State.filterText = "";
        ResetRecipeListPosition();
        SelectFirstFilteredRecipe();
        CraftingOrderFrame_Update();
        return;
    end

    State.filterText = text;
    ResetRecipeListPosition();
    SelectFirstFilteredRecipe();
    CraftingOrderFrame_Update();
end

-----------------------------------------------------------------------
-- "Only show makeable" checkbox
-----------------------------------------------------------------------

function CraftingOrderOnlyShowMakeable(checked)
    State.onlyMakeable = checked;
    -- Reset scroll position when filtering
    ResetRecipeListPosition();
    SelectFirstFilteredRecipe();
    CraftingOrderFrame_Update();
end

-----------------------------------------------------------------------
-- Inventory slot category dropdown
-----------------------------------------------------------------------

function CraftingOrderSubClassDropDown_OnLoad()
    local frame = this or CraftingOrderSubClassDropDown;
    UIDropDownMenu_Initialize(frame, CraftingOrderSubClassDropDown_Initialize);
    UIDropDownMenu_SetWidth(120, frame);
    UIDropDownMenu_SetSelectedID(frame, 1);
    UIDropDownMenu_SetText(ALL_SUBCLASSES or ALL, frame);
end

function CraftingOrderSubClassDropDown_OnShow()
    local frame = this or CraftingOrderSubClassDropDown;
    UIDropDownMenu_Initialize(frame, CraftingOrderSubClassDropDown_Initialize);
end

function CraftingOrderSubClassDropDown_Initialize()
    local info = UIDropDownMenu_CreateInfo();

    for i, subClass in ipairs(State.subclasses) do
        local displayText = subClass == "ALL" and (ALL_SUBCLASSES or ALL) or subClass;
        info.text = displayText;
        info.value = subClass;
        info.func = CraftingOrderSubClassDropDownButton_OnClick;
        info.checked = State.subclassFilter == subClass;
        UIDropDownMenu_AddButton(info);
    end
end

function CraftingOrderSubClassDropDownButton_OnClick()
    local button = this;
    if not button then return; end
    UIDropDownMenu_SetSelectedID(CraftingOrderSubClassDropDown, button:GetID());
    State.subclassFilter = button.value or "ALL";
    UIDropDownMenu_SetText(State.subclassFilter == "ALL" and (ALL_SUBCLASSES or ALL) or State.subclassFilter, CraftingOrderSubClassDropDown);
    ResetRecipeListPosition();
    SelectFirstFilteredRecipe();
    CraftingOrderFrame_Update();
end

function CraftingOrderCategoryDropDown_OnLoad()
    local frame = this or CraftingOrderCategoryDropDown;
    UIDropDownMenu_Initialize(frame, CraftingOrderCategoryDropDown_Initialize);
    UIDropDownMenu_SetWidth(120, frame);
    UIDropDownMenu_SetSelectedID(frame, 1);
    UIDropDownMenu_SetText(ALL_INVENTORY_SLOTS or ALL, frame);
end

function CraftingOrderCategoryDropDown_OnShow()
    local frame = this or CraftingOrderCategoryDropDown;
    UIDropDownMenu_Initialize(frame, CraftingOrderCategoryDropDown_Initialize);
end

function CraftingOrderCategoryDropDown_Initialize()
    local info = UIDropDownMenu_CreateInfo();

    for i, category in ipairs(State.categories) do
        local displayText = category == "ALL" and (ALL_INVENTORY_SLOTS or ALL) or category;
        info.text = displayText;
        info.value = category;
        info.func = CraftingOrderCategoryDropDownButton_OnClick;
        info.checked = State.categoryFilter == category;
        UIDropDownMenu_AddButton(info);
    end
end

function CraftingOrderCategoryDropDownButton_OnClick()
    local button = this;
    if not button then return; end
    UIDropDownMenu_SetSelectedID(CraftingOrderCategoryDropDown, button:GetID());
    State.categoryFilter = button.value or "ALL";
    UIDropDownMenu_SetText(State.categoryFilter == "ALL" and (ALL_INVENTORY_SLOTS or ALL) or State.categoryFilter, CraftingOrderCategoryDropDown);
    ResetRecipeListPosition();
    SelectFirstFilteredRecipe();
    CraftingOrderFrame_Update();
end

-----------------------------------------------------------------------
-- Collapse/Expand all
-----------------------------------------------------------------------

SetCollapseAllButtonState = function(collapsed)
    local button = CraftingOrderCollapseAllButton;
    if not button then return; end
    button.collapsed = collapsed and true or nil;
    if collapsed then
        button:SetNormalTexture("Interface\\Buttons\\UI-PlusButton-Up");
    else
        button:SetNormalTexture("Interface\\Buttons\\UI-MinusButton-Up");
    end
end

function CraftingOrderCollapseAllButton_OnClick()
    local button = this or CraftingOrderCollapseAllButton;
    local shouldCollapse = not button.collapsed;
    SetCollapseAllButtonState(shouldCollapse);

    State.collapsedHeaders = {};
    if shouldCollapse then
        for _, rec in ipairs(State.recipes) do
            State.collapsedHeaders[rec.subClass or "Other"] = true;
        end
    end

    State.selectedRecipeIndex = 0;
    ResetRecipeListPosition();
    CraftingOrderFrame_SetSelection(0);
    CraftingOrderFrame_Update();
end

-----------------------------------------------------------------------
-- Quantity increment / decrement
-----------------------------------------------------------------------

function CraftingOrderFrameIncrement_OnClick()
    if CraftingOrderInputBox:GetNumber() < 100 then
        CraftingOrderInputBox:SetNumber(CraftingOrderInputBox:GetNumber() + 1);
    end
end

function CraftingOrderFrameDecrement_OnClick()
    if CraftingOrderInputBox:GetNumber() > 1 then
        CraftingOrderInputBox:SetNumber(CraftingOrderInputBox:GetNumber() - 1);
    end
end

-----------------------------------------------------------------------
-- Item tooltip on hover
-----------------------------------------------------------------------

function CraftingOrderItem_OnEnter(self)
    self = self or this;
    if State.selectedRecipeIndex > 0 then
        local rec = CraftingOrder_GetRecipeByFilteredIndex(State.selectedRecipeIndex);
        if rec then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
            local targetLink = GetEnchantTargetLink(rec);
            if targetLink then
                GameTooltip:SetHyperlink(targetLink);
            elseif rec.itemId and rec.itemId > 0 then
                GameTooltip:SetHyperlink("item:" .. rec.itemId .. ":0:0:0:0:0:0:0");
            elseif rec.spellId and rec.spellId > 0 then
                GameTooltip:SetHyperlink("spell:" .. rec.spellId);
            end
        end
    end
    CursorUpdate(self);
end

-----------------------------------------------------------------------
-- Create / Create All — send craft messages to server
-----------------------------------------------------------------------

function CraftingOrderCancelButton_OnClick()
    if State.selectingEnchantTarget then
        State.selectingEnchantTarget = false;
        State.selectedEnchantSpellId = nil;
        State.selectedEnchantSpellName = nil;
        if State.baseEnchantRecipes then
            State.recipes = State.baseEnchantRecipes;
        end
        State.baseEnchantRecipes = nil;
        RebuildRecipeCategories();
        ResetRecipeListPosition();
        SelectFirstFilteredRecipe();
        UpdateCancelButton();
        CraftingOrderFrame_Update();
        return;
    end

    CloseCraftingOrderFrame();
end

function CraftingOrderFrameCloseButton_OnClick()
    CloseCraftingOrderFrame();
end

local OriginalCraftingOrderFrame_SetSelection = CraftingOrderFrame_SetSelectionBase;
local OriginalCraftingOrderSkillButton_OnClick = CraftingOrderSkillButton_OnClickBase;

function OnServerMessage(opcode, payload, page, totalPages)
    local op = opcode or "";
    payload = payload or "";
    page = tonumber(page) or 0;
    totalPages = tonumber(totalPages) or 1;

    local function ShowResult(message, good)
        if UIErrorsFrame and UIErrorsFrame.AddMessage then
            if good then
                UIErrorsFrame:AddMessage(message or "", 1.0, 1.0, 0.0, 1.0, 5);
            else
                UIErrorsFrame:AddMessage(message or "", 1.0, 0.0, 0.0, 1.0, 5);
            end
        end
    end

    if op == "INIT" then
        local parts = SplitString(payload, "\t");
        local service = tonumber(parts[3]) or 0;
        local uiMode = tonumber(parts[4]) or 0;

        ResetUiModes();
        State.professionId = tonumber(parts[1]) or 0;
        State.professionName = parts[2] or "Crafting";
        State.isEnchantMode = service == 2 and uiMode ~= 1;
        State.isDisenchantMode = service == 3;
        State.isHandInMode = uiMode == 1;
        State.recipes = {};
        State.selectedRecipeIndex = 0;
        State.onlyMakeable = false;
        State.filterText = "";
        State.subclassFilter = "ALL";
        State.categoryFilter = "ALL";
        State.subclasses = { "ALL" };
        State.categories = { "ALL" };
        State.collapsedHeaders = {};
        State.recipeBuffer = "";
        SetCollapseAllButtonState(false);

        CraftingOrderFrameTitleText:SetText("Crafting Orders");
        CraftingOrderRankFrameSkillRank:SetText(State.professionName);
        CraftingOrderRankFrame:SetMinMaxValues(0, 1);
        CraftingOrderRankFrame:SetValue(1);
        SetPortraitTexture(CraftingOrderFramePortrait, "player");

        if not CraftingOrderFrame:IsVisible() then
            CraftingOrderFrame:Show();
        end

        CraftingOrderCreateButton:Disable();
        CraftingOrderCreateAllButton:Disable();
        CraftingOrderFrameEditBox:SetText(SEARCH);
        CraftingOrderFrameAvailableFilterCheckButton:SetChecked(false);
        UpdateCancelButton();

        if State.isHandInMode then
            CraftingOrderCreateAllButton:Hide();
            CraftingOrderDecrementButton:Hide();
            CraftingOrderInputBox:Hide();
            CraftingOrderIncrementButton:Hide();
            CraftingOrderCreateButton:SetText("Hand in");
        elseif State.isDisenchantMode then
            CraftingOrderCreateAllButton:Hide();
            CraftingOrderDecrementButton:Hide();
            CraftingOrderInputBox:Hide();
            CraftingOrderIncrementButton:Hide();
            CraftingOrderCreateButton:SetText("Disenchant");
        elseif State.isEnchantMode then
            CraftingOrderCreateAllButton:Hide();
            CraftingOrderDecrementButton:Hide();
            CraftingOrderInputBox:Hide();
            CraftingOrderIncrementButton:Hide();
            CraftingOrderCreateButton:SetText(ACCEPT or "Apply");
        else
            CraftingOrderCreateButton:SetText(CREATE);
        end

        RequestCraftingListPage(0);
        return;
    end

    if op == "CLOSE_UI" then
        if CraftingOrderFrame:IsVisible() then
            HideUIPanel(CraftingOrderFrame);
        end
        return;
    end

    if op == "RECIPES" then
        if page == 0 then
            State.recipes = {};
            State.selectedRecipeIndex = 0;
            State.baseEnchantRecipes = nil;
        end
        local parsed = BuildRecipesFromString(payload);
        for _, rec in ipairs(parsed) do
            table.insert(State.recipes, rec);
        end

        if page + 1 < totalPages then
            RequestCraftingListPage(page + 1);
            return;
        end

        State.recipeBuffer = "";
        RebuildRecipeCategories();
        if State.isEnchantMode then
            State.baseEnchantRecipes = State.recipes;
        end
        ResetRecipeListPosition();
        SelectFirstFilteredRecipe();
        CraftingOrderFrame_Update();
        UpdateCancelButton();
        return;
    end

    if op == "HANDIN" then
        if page == 0 then
            State.recipes = {};
            State.selectedRecipeIndex = 0;
        end
        State.isHandInMode = true;
        State.isEnchantMode = false;
        local parsed = BuildHandInRecords(payload);
        for _, rec in ipairs(parsed) do
            table.insert(State.recipes, rec);
        end

        if page + 1 < totalPages then
            RequestCraftingListPage(page + 1);
            return;
        end

        RebuildRecipeCategories();
        ResetRecipeListPosition();
        SelectFirstFilteredRecipe();
        CraftingOrderFrame_Update();
        UpdateCancelButton();
        return;
    end

    if op == "CRAFT_RESULT" or op == "ENCHANT_RESULT" or op == "DISENCHANT_RESULT" or op == "HANDIN_RESULT" then
        local result = SplitString(payload, "\t");
        local ok = result[1] == "OK";
        local message = result[2] or payload;
        ShowResult(message, ok);

        if op == "HANDIN_RESULT" then
            State.isHandInMode = true;
            State.isEnchantMode = false;
        end

        if ok then
            RefreshCurrentView();
        elseif op == "DISENCHANT_RESULT" then
            RefreshCurrentView();
        end
        return;
    end

    if op == "ENCHANT_TARGETS" then
        local baseRecipe = FindRecipeBySpellId(State.baseEnchantRecipes or State.recipes, State.selectedEnchantSpellId);
        local spellId, targetRecipes = BuildEnchantTargetRecipes(payload, baseRecipe);
        if table.getn(targetRecipes) == 0 then
            ShowResult("No matching item found.", false);
            return;
        end

        State.selectingEnchantTarget = true;
        State.baseEnchantRecipes = State.baseEnchantRecipes or State.recipes;
        State.recipes = targetRecipes;
        State.selectedRecipeIndex = 0;
        RebuildRecipeCategories();
        ResetRecipeListPosition();
        SelectFirstFilteredRecipe();
        UpdateCancelButton();
        CraftingOrderFrame_Update();
        return;
    end

    if op == "ERROR" then
        ShowResult(payload, false);
        if payload == "protocol version mismatch" and CraftingOrderFrame:IsVisible() then
            HideUIPanel(CraftingOrderFrame);
        end
    end
end

function CraftingOrderFrame_Show()
    ShowUIPanel(CraftingOrderFrame);
    CraftingOrderCreateButton:Disable();
    CraftingOrderCreateAllButton:Disable();
    CraftingOrderFrameEditBox:SetText(SEARCH);
    CraftingOrderFrameAvailableFilterCheckButton:SetChecked(false);
    State.onlyMakeable = false;
    State.filterText = "";
    if State.selectedRecipeIndex == 0 and table.getn(State.recipes) > 0 then
        SelectFirstFilteredRecipe();
    elseif State.selectedRecipeIndex > 0 then
        CraftingOrderFrame_SetSelection(State.selectedRecipeIndex);
    end
    FauxScrollFrame_SetOffset(CraftingOrderListScrollFrame, 0);
    CraftingOrderListScrollFrameScrollBar:SetMinMaxValues(0, 0);
    CraftingOrderListScrollFrameScrollBar:SetValue(0);
    SetPortraitTexture(CraftingOrderFramePortrait, "player");
    CraftingOrderFrame_Update();
    CraftingOrderFrame:SetScript("OnUpdate", CraftingOrderFrame_OnUpdate);
    State.rangeCheckElapsed = 0;
    UpdateCancelButton();
end

function CraftingOrderFrame_Hide()
    CloseCraftingOrderFrame();
end

function CraftingOrderFrame_OnUpdate(self, elapsed)
    self = self or this or CraftingOrderFrame;
    elapsed = elapsed or arg1 or 0;
    if not self:IsVisible() then
        return;
    end

    State.rangeCheckElapsed = (State.rangeCheckElapsed or 0) + elapsed;
    if State.rangeCheckElapsed < 0.25 then
        return;
    end
    State.rangeCheckElapsed = 0;

    local selectedRecipe = CraftingOrderFrame.cooldownRecipe;
    if selectedRecipe and selectedRecipe.cooldownEndsAt then
        if UpdateRecipeCooldownDisplay(selectedRecipe) then
            CraftingOrderCreateButton:Disable();
            CraftingOrderCreateAllButton:Disable();
        else
            CraftingOrderFrame.cooldownRecipe = nil;
            CraftingOrderFrame_SetSelection(State.selectedRecipeIndex);
        end
    end

    -- Do not infer session validity from target/npc unit tokens.  The server
    -- owns range/session validation, and gossip closure commonly clears both
    -- tokens immediately after Browse or Hand-in is selected.
end

function CraftingOrderFrame_SetSelection(index)
    if index == 0 then
        return OriginalCraftingOrderFrame_SetSelection(index);
    end

    OriginalCraftingOrderFrame_SetSelection(index);

    if State.isEnchantMode then
        CraftingOrderCreateAllButton:Hide();
        CraftingOrderDecrementButton:Hide();
        CraftingOrderInputBox:Hide();
        CraftingOrderIncrementButton:Hide();
        CraftingOrderCreateButton:SetText(ACCEPT or "Apply");
    elseif State.isDisenchantMode or State.isMillingMode or State.isProspectingMode then
        CraftingOrderCreateAllButton:Hide();
        CraftingOrderDecrementButton:Hide();
        CraftingOrderInputBox:Hide();
        CraftingOrderIncrementButton:Hide();
        if State.isDisenchantMode then
            CraftingOrderCreateButton:SetText("Disenchant");
        elseif State.isMillingMode then
            CraftingOrderCreateButton:SetText("Mill");
        else
            CraftingOrderCreateButton:SetText("Prospect");
        end
    elseif State.isHandInMode then
        CraftingOrderCreateAllButton:Hide();
        CraftingOrderDecrementButton:Hide();
        CraftingOrderInputBox:Hide();
        CraftingOrderIncrementButton:Hide();
        CraftingOrderCreateButton:SetText("Hand in");
    end
end

function CraftingOrderSkillButton_OnClick(self, button)
    self = self or this;
    button = button or arg1;
    if button ~= "LeftButton" then
        return;
    end

    if State.selectingEnchantTarget then
        local rec = CraftingOrder_GetRecipeByFilteredIndex(self:GetID());
        if rec and rec.isEnchantTarget then
            CraftingOrderFrame_SetSelection(self:GetID());
            CraftingOrderFrame_Update();
            return;
        end
    end

    OriginalCraftingOrderSkillButton_OnClick(self, button);
end

function CraftingOrderCreateButton_OnClick()
    if State.selectedRecipeIndex == 0 then return; end
    local rec = CraftingOrder_GetRecipeByFilteredIndex(State.selectedRecipeIndex);
    if not rec then return; end

    if State.isHandInMode then
        SendToServer("HANDIN\t" .. (rec.itemGuid or rec.spellId));
        return;
    end

    if State.isEnchantMode then
        if State.selectingEnchantTarget and rec.isEnchantTarget then
            ShowEnchantConfirm(rec.spellId, State.selectedEnchantSpellName, rec.targetBag, rec.targetSlot, rec.targetName, rec.rawItemName or rec.itemName);
        else
            State.selectedEnchantSpellId = rec.spellId;
            State.selectedEnchantSpellName = rec.itemName;
            SendToServer("REQUEST_ENCHANT_TARGETS\t" .. rec.spellId);
        end
        return;
    end

    if State.isDisenchantMode then
        if rec.targetBag ~= nil and rec.targetSlot ~= nil then
            SendToServer("DISENCHANT\t" .. rec.targetBag .. "\t" .. rec.targetSlot);
        end
        return;
    end

    if State.isMillingMode then
        if rec.targetBag ~= nil and rec.targetSlot ~= nil then
            SendToServer("MILL\t" .. rec.targetBag .. "\t" .. rec.targetSlot);
        end
        return;
    end

    if State.isProspectingMode then
        if rec.targetBag ~= nil and rec.targetSlot ~= nil then
            SendToServer("PROSPECT\t" .. rec.targetBag .. "\t" .. rec.targetSlot);
        end
        return;
    end

    local quantity = tonumber(CraftingOrderInputBox:GetText()) or 1;
    if quantity < 1 then quantity = 1; end
    SendToServer("CRAFT\t" .. rec.spellId .. "\t" .. quantity);
end

function CraftingOrderCreateAllButton_OnClick()
    if State.isEnchantMode or State.isDisenchantMode or State.isMillingMode or State.isProspectingMode or State.isHandInMode then
        return;
    end

    if State.selectedRecipeIndex == 0 then return; end
    local rec = CraftingOrder_GetRecipeByFilteredIndex(State.selectedRecipeIndex);
    if not rec then return; end

    local available = rec.numAvailable or 1;
    if available < 1 then available = 1; end

    CraftingOrderInputBox:SetNumber(available);
    SendToServer("CRAFT\t" .. rec.spellId .. "\t" .. available);
end

-----------------------------------------------------------------------
-- Versioned CO protocol adapter for the Vanilla/Turtle server.
-----------------------------------------------------------------------

local function HandleCraftingOrderPacket(payload)
    local fields = SplitString(payload or "", "\t");
    if table.getn(fields) < 7 then
        return;
    end

    local version = tonumber(fields[1]) or 0;
    local requestId = tonumber(fields[2]) or 0;
    local part = tonumber(fields[3]) or 0;
    local total = tonumber(fields[4]) or 0;
    local opcode = fields[5] or "";
    local page = tonumber(fields[6]) or 0;
    local totalPages = tonumber(fields[7]) or 1;

    if version ~= CRAFTING_ORDER_PROTOCOL_VERSION then
        if UIErrorsFrame and UIErrorsFrame.AddMessage then
            UIErrorsFrame:AddMessage("Crafting Orders: protocol version mismatch", 1.0, 0.0, 0.0, 1.0, 5);
        end
        return;
    end
    -- The server opens a session with an INIT packet whose request ID is 0;
    -- every response to a client request must carry a positive ID.
    if (requestId == 0 and opcode ~= "INIT") or part < 1 or total < 1 or part > total then
        return;
    end

    local assembled = nil;
    if total == 1 then
        assembled = "";
        for i = 8, table.getn(fields) do
            if i > 8 then
                assembled = assembled .. "\t";
            end
            assembled = assembled .. fields[i];
        end
    else
        local chunk = "";
        for i = 8, table.getn(fields) do
            if i > 8 then
                chunk = chunk .. "\t";
            end
            chunk = chunk .. fields[i];
        end

        if CraftingOrderChunkState.requestId ~= requestId or
            CraftingOrderChunkState.opcode ~= opcode or
            CraftingOrderChunkState.page ~= page then
            CraftingOrderChunkState.requestId = requestId;
            CraftingOrderChunkState.opcode = opcode;
            CraftingOrderChunkState.page = page;
            CraftingOrderChunkState.totalPages = totalPages;
            CraftingOrderChunkState.total = total;
            CraftingOrderChunkState.parts = {};
        end

        CraftingOrderChunkState.parts[part] = chunk;
        for i = 1, total do
            if not CraftingOrderChunkState.parts[i] then
                return;
            end
        end

        assembled = "";
        for i = 1, total do
            assembled = assembled .. CraftingOrderChunkState.parts[i];
        end
        CraftingOrderChunkState.parts = {};
        CraftingOrderChunkState.total = 0;
    end

    OnServerMessage(opcode, assembled, page, totalPages);
end

function CraftingOrderFrame_OnEvent(self, eventName, eventArg1, eventArg2, eventArg3, eventArg4)
    if type(eventName) ~= "string" then
        eventName = event;
        eventArg1 = arg1;
        eventArg2 = arg2;
        eventArg3 = arg3;
        eventArg4 = arg4;
    end
    if eventName == "PLAYER_LOGIN" then
        RegisterCraftingOrderPrefixes();
        return;
    end
    if eventName == "GET_ITEM_INFO_RECEIVED" then
        RebuildRecipeCategories();
        SelectFirstFilteredRecipe();
        CraftingOrderFrame_Update();
        return;
    end
    if eventName ~= "CHAT_MSG_ADDON" then
        return;
    end

    local prefix, message = eventArg1, eventArg2;
    if prefix == "CO" then
        HandleCraftingOrderPacket(message);
    end
end

function CraftingOrderFrame_OnLoad(self)
    self = self or this or CraftingOrderFrame;
    self:SetScript("OnUpdate", CraftingOrderFrame_OnUpdate);
    RegisterCraftingOrderEscapeFrame();
    if CraftingOrderCancelButton then
        CraftingOrderCancelButton:SetScript("OnClick", CraftingOrderCancelButton_OnClick);
    end
    if CraftingOrderFrameCloseButton then
        CraftingOrderFrameCloseButton:SetScript("OnClick", CraftingOrderFrameCloseButton_OnClick);
    end
end

-- Keep event delivery on a dedicated Lua frame.  Turtle's 1.12 client uses
-- the legacy global event/argN variables for XML handlers; a Lua frame gives
-- us the same callback shape as the working ModReagentBank addon while the
-- handler above still normalizes both legacy and newer callback arguments.
local CraftingOrderEventFrame = CreateFrame("Frame");
CraftingOrderEventFrame:RegisterEvent("PLAYER_LOGIN");
CraftingOrderEventFrame:RegisterEvent("CHAT_MSG_ADDON");
pcall(CraftingOrderEventFrame.RegisterEvent, CraftingOrderEventFrame, "GET_ITEM_INFO_RECEIVED");
CraftingOrderEventFrame:SetScript("OnEvent", function(self, eventName, eventArg1, eventArg2, eventArg3, eventArg4)
    CraftingOrderFrame_OnEvent(self, eventName, eventArg1, eventArg2, eventArg3, eventArg4);
end);
