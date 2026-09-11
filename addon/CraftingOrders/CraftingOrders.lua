-- Vanilla/Turtle 1.12 Crafting Orders UI using Lua 5.0 and this/event/argN.

CRAFTING_ORDERS_PROTOCOL = 2;
CRAFTING_ORDERS_MAX_ROWS = 12;

local MODE_BROWSE = 0;
local MODE_HANDIN = 1;
local MODE_ENCHANT_TARGETS = 2;

local State = {
    reqId = 1,
    professionId = 0,
    professionName = "",
    service = 0,
    mode = MODE_BROWSE,
    recipes = {},
    selected = 0,
    chunks = {},
    chunkOpcode = "",
    chunkReq = 0,
    chunkTotal = 0,
    listFilter = "",
    listTier = 0,
    pendingPages = false,
    viewOffset = 0,
    quantity = 0,
    pendingEnchant = nil,
};

local function tlen(t)
    if not t then
        return 0;
    end
    return table.getn(t);
end

local function split(str, delim)
    local out = {};
    if not str then
        return out;
    end
    local start = 1;
    local delimLen = string.len(delim);
    while true do
        local s, e = string.find(str, delim, start, true);
        if not s then
            table.insert(out, string.sub(str, start));
            break;
        end
        table.insert(out, string.sub(str, start, s - 1));
        start = e + 1;
    end
    return out;
end

-- Split on an unescaped single-character delimiter; keep escape sequences intact.
local function splitEscaped(str, delim)
    local out = {};
    if not str then
        return out;
    end
    local current = "";
    local escaped = false;
    local i;
    local n = string.len(str);
    for i = 1, n do
        local ch = string.sub(str, i, i);
        if escaped then
            current = current.."\\"..ch;
            escaped = false;
        elseif ch == "\\" then
            escaped = true;
        elseif ch == delim then
            table.insert(out, current);
            current = "";
        else
            current = current..ch;
        end
    end
    if escaped then
        current = current.."\\";
    end
    table.insert(out, current);
    return out;
end

-- Character-wise unescape matching CraftingOrdersDomain::UnescapeField.
local function unescape(text)
    if not text then
        return "";
    end
    local out = "";
    local escaped = false;
    local i;
    local n = string.len(text);
    for i = 1, n do
        local ch = string.sub(text, i, i);
        if escaped then
            if ch == "n" then
                out = out.."\n";
            elseif ch == "r" then
                out = out.."\r";
            elseif ch == "t" then
                out = out.."\t";
            else
                out = out..ch;
            end
            escaped = false;
        elseif ch == "\\" then
            escaped = true;
        else
            out = out..ch;
        end
    end
    return out;
end

local function SendCO(opcode, payload)
    State.reqId = State.reqId + 1;
    local body = tostring(CRAFTING_ORDERS_PROTOCOL).."\t"..tostring(State.reqId).."\t"..opcode;
    if payload and payload ~= "" then
        body = body.."\t"..payload;
    end
    -- Vanilla 1.12 cannot send addon whispers.  Tortoise consumes addon
    -- messages sent on the guild chat path before normal chat handling.
    SendAddonMessage("CO", body, "GUILD");
end

local function SetStatus(text)
    if CraftingOrdersFrameStatus then
        CraftingOrdersFrameStatus:SetText(text or "");
    end
end

local function SetActionText(text)
    local btn = CraftingOrdersFrameCraft;
    if btn then
        btn:SetText(text or "Create");
    end
end

local function SetButtonEnabled(button, enabled)
    if not button then
        return;
    end
    if enabled then
        button:Enable();
    else
        button:Disable();
    end
end

local function SetQuantity(value)
    local recipe = State.recipes[State.selected];
    local maximum = recipe and tonumber(recipe.available or 0) or 0;
    if recipe and recipe.cooldown and recipe.cooldown > 0 then
        maximum = 0;
    end
    value = tonumber(value or 0) or 0;
    if maximum < 0 then
        maximum = 0;
    end
    if value < 0 then
        value = 0;
    end
    if value > maximum then
        value = maximum;
    end
    State.quantity = value;
    if CraftingOrdersFrameQuantity then
        CraftingOrdersFrameQuantity:SetText("Qty: "..tostring(value).."/"..tostring(maximum));
    end
    SetButtonEnabled(CraftingOrdersFrameQuantityDown, value > 1);
    SetButtonEnabled(CraftingOrdersFrameQuantityUp, value < maximum);
end

local function UpdateNavigation()
    local count = tlen(State.recipes);
    local first = State.viewOffset + 1;
    local last = State.viewOffset + CRAFTING_ORDERS_MAX_ROWS;
    if last > count then
        last = count;
    end
    if CraftingOrdersFramePage then
        if count == 0 then
            CraftingOrdersFramePage:SetText("0-0 / 0");
        else
            CraftingOrdersFramePage:SetText(tostring(first).."-"..tostring(last).." / "..tostring(count));
        end
    end
    SetButtonEnabled(CraftingOrdersFramePrev, State.viewOffset > 0);
    SetButtonEnabled(CraftingOrdersFrameNext, last < count);
end

local function UpdateListOptions()
    local enabled = State.mode ~= MODE_HANDIN and State.service ~= 3 and not State.pendingPages;
    if CraftingOrdersFrameFilter then
        if State.listFilter == "MAKEABLE" then
            CraftingOrdersFrameFilter:SetText("Makeable");
        else
            CraftingOrdersFrameFilter:SetText("All items");
        end
    end
    if CraftingOrdersFrameTier then
        local names = { "All tiers", "Apprentice", "Journeyman", "Expert", "Artisan" };
        CraftingOrdersFrameTier:SetText(names[State.listTier + 1] or "All tiers");
    end
    SetButtonEnabled(CraftingOrdersFrameFilter, enabled);
    SetButtonEnabled(CraftingOrdersFrameTier, enabled);
end

local function ParseRecipe(entry)
    local groups = splitEscaped(entry, ";");
    if tlen(groups) < 1 then
        return nil;
    end
    local head = splitEscaped(groups[1], ",");
    local recipe = {
        spellId = tonumber(head[1] or "0") or 0,
        itemId = tonumber(head[2] or "0") or 0,
        name = unescape(head[3] or ""),
        tier = tonumber(head[4] or "0") or 0,
        reqRank = tonumber(head[5] or "0") or 0,
        fee = tonumber(head[6] or "0") or 0,
        made = tonumber(head[7] or "1") or 1,
        available = tonumber(head[8] or "0") or 0,
        subclass = unescape(head[9] or ""),
        bag = State.service == 3 and (tonumber(head[12] or "0") or 0) or 0,
        slot = State.service == 3 and (tonumber(head[13] or "0") or 0) or 0,
        cooldown = State.service ~= 3 and (tonumber(head[13] or "0") or 0) or 0,
        materials = {},
        kind = "recipe",
    };
    local i;
    for i = 2, tlen(groups) do
        local mat = splitEscaped(groups[i], ",");
        table.insert(recipe.materials, {
            itemId = tonumber(mat[1] or "0") or 0,
            count = tonumber(mat[2] or "0") or 0,
            name = unescape(mat[3] or ""),
            have = tonumber(mat[5] or "0") or 0,
        });
    end
    return recipe;
end

local function ParseHandIn(entry)
    local head = splitEscaped(entry, ",");
    local guid = tonumber(head[1] or "0") or 0;
    if guid == 0 then
        return nil;
    end
    return {
        itemGuid = guid,
        spellId = guid,
        itemId = tonumber(head[2] or "0") or 0,
        name = unescape(head[3] or ""),
        taughtSpell = tonumber(head[4] or "0") or 0,
        createdItemId = tonumber(head[5] or "0") or 0,
        fee = 0,
        available = 1,
        reqRank = 0,
        materials = {},
        kind = "handin",
    };
end

local function ParseEnchantTarget(entry, spellId)
    local fields = splitEscaped(entry, "^");
    local bag = tonumber(fields[1] or "0") or 0;
    local slot = tonumber(fields[2] or "0") or 0;
    return {
        spellId = spellId,
        bag = bag,
        slot = slot,
        name = unescape(fields[3] or "").." - "..unescape(fields[5] or ""),
        itemId = tonumber(fields[4] or "0") or 0,
        fee = 0,
        available = 1,
        reqRank = 0,
        materials = {},
        kind = "enchant_target",
    };
end

local function RefreshList()
    local i;
    for i = 1, CRAFTING_ORDERS_MAX_ROWS do
        local btn = getglobal("CraftingOrdersRow"..i);
        if btn then
            local absoluteIndex = State.viewOffset + i;
            local recipe = State.recipes[absoluteIndex];
            if recipe then
                btn:SetText(recipe.name);
                btn.idx = absoluteIndex;
                btn:Show();
            else
                btn:SetText("");
                btn:Hide();
            end
        end
    end
    CraftingOrders_Select(State.selected);
    UpdateNavigation();
    UpdateListOptions();
end

function CraftingOrders_Select(index)
    State.selected = index or 0;
    local recipe = State.recipes[State.selected];
    local detail = CraftingOrdersFrameDetail;
    if not detail then
        return;
    end
    if not recipe then
        detail:SetText("");
        SetQuantity(0);
        return;
    end
    if recipe.kind == "handin" then
        detail:SetText(recipe.name.."\nHand in this formula to unlock the recipe.");
        SetQuantity(0);
        return;
    end
    if recipe.kind == "enchant_target" then
        detail:SetText(recipe.name.."\nSelect this item, then click Enchant.");
        SetQuantity(0);
        return;
    end
    local lines = recipe.name.."\nSkill: "..recipe.reqRank.."\nFee: "..recipe.fee.."c\nCan make: "..recipe.available.."\n";
    if recipe.cooldown and recipe.cooldown > 0 then
        lines = lines.."Cooldown: active\n";
    end
    local i;
    for i = 1, tlen(recipe.materials) do
        local mat = recipe.materials[i];
        lines = lines.."\n"..mat.count.."x "..mat.name.." ("..mat.have..")";
    end
    detail:SetText(lines);
    if State.quantity == 0 or State.quantity > recipe.available then
        SetQuantity(recipe.available > 0 and 1 or 0);
    else
        SetQuantity(State.quantity);
    end
end

function CraftingOrders_OnLoad()
    if RegisterAddonMessagePrefix then
        RegisterAddonMessagePrefix("CO");
    end
    local i;
    for i = 1, CRAFTING_ORDERS_MAX_ROWS do
        local btn = CreateFrame("Button", "CraftingOrdersRow"..i, CraftingOrdersFrame, "UIPanelButtonTemplate");
        btn:SetWidth(230);
        btn:SetHeight(20);
        btn:SetPoint("TOPLEFT", CraftingOrdersFrame, "TOPLEFT", 24, -30 - (i * 22));
        btn:SetText("");
        btn.idx = i;
        btn:SetScript("OnClick", function()
            CraftingOrders_Select(this.idx);
        end);
        btn:Hide();
    end

    local page = CraftingOrdersFrame:CreateFontString("CraftingOrdersFramePage", "ARTWORK", "GameFontHighlightSmall");
    page:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 88, 26);
    page:SetWidth(85);
    page:SetJustifyH("CENTER");
    page:SetText("0-0 / 0");

    local prev = CreateFrame("Button", "CraftingOrdersFramePrev", CraftingOrdersFrame, "UIPanelButtonTemplate");
    prev:SetWidth(58);
    prev:SetHeight(22);
    prev:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 24, 20);
    prev:SetText("Prev");
    prev:SetScript("OnClick", function() CraftingOrders_PreviousPage(); end);

    local next = CreateFrame("Button", "CraftingOrdersFrameNext", CraftingOrdersFrame, "UIPanelButtonTemplate");
    next:SetWidth(58);
    next:SetHeight(22);
    next:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 180, 20);
    next:SetText("Next");
    next:SetScript("OnClick", function() CraftingOrders_NextPage(); end);

    local filter = CreateFrame("Button", "CraftingOrdersFrameFilter", CraftingOrdersFrame, "UIPanelButtonTemplate");
    filter:SetWidth(100);
    filter:SetHeight(22);
    filter:SetPoint("TOPLEFT", CraftingOrdersFrame, "TOPLEFT", 24, -22);
    filter:SetScript("OnClick", function() CraftingOrders_ToggleFilter(); end);

    local tier = CreateFrame("Button", "CraftingOrdersFrameTier", CraftingOrdersFrame, "UIPanelButtonTemplate");
    tier:SetWidth(105);
    tier:SetHeight(22);
    tier:SetPoint("TOPLEFT", CraftingOrdersFrame, "TOPLEFT", 130, -22);
    tier:SetScript("OnClick", function() CraftingOrders_CycleTier(); end);

    local down = CreateFrame("Button", "CraftingOrdersFrameQuantityDown", CraftingOrdersFrame, "UIPanelButtonTemplate");
    down:SetWidth(28);
    down:SetHeight(22);
    down:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 255, 20);
    down:SetText("-");
    down:SetScript("OnClick", function() SetQuantity(State.quantity - 1); end);

    local quantity = CraftingOrdersFrame:CreateFontString("CraftingOrdersFrameQuantity", "ARTWORK", "GameFontHighlightSmall");
    quantity:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 285, 26);
    quantity:SetWidth(60);
    quantity:SetJustifyH("CENTER");
    quantity:SetText("Qty: 0/0");

    local up = CreateFrame("Button", "CraftingOrdersFrameQuantityUp", CraftingOrdersFrame, "UIPanelButtonTemplate");
    up:SetWidth(28);
    up:SetHeight(22);
    up:SetPoint("BOTTOMLEFT", CraftingOrdersFrame, "BOTTOMLEFT", 348, 20);
    up:SetText("+");
    up:SetScript("OnClick", function() SetQuantity(State.quantity + 1); end);

    StaticPopupDialogs = StaticPopupDialogs or {};
    StaticPopupDialogs["CRAFTING_ORDERS_ENCHANT_CONFIRM"] = {
        text = "Apply this enchant? An existing enchant may be overwritten.",
        button1 = "Apply",
        button2 = "Cancel",
        OnAccept = function() CraftingOrders_ConfirmEnchant(); end,
        OnCancel = function() State.pendingEnchant = nil; end,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    };

    UpdateListOptions();
    UpdateNavigation();
end

function CraftingOrders_PreviousPage()
    if State.viewOffset <= 0 then
        return;
    end
    State.viewOffset = State.viewOffset - CRAFTING_ORDERS_MAX_ROWS;
    if State.viewOffset < 0 then
        State.viewOffset = 0;
    end
    if State.recipes[State.viewOffset + 1] then
        State.selected = State.viewOffset + 1;
    end
    RefreshList();
end

function CraftingOrders_NextPage()
    if State.viewOffset + CRAFTING_ORDERS_MAX_ROWS >= tlen(State.recipes) then
        return;
    end
    State.viewOffset = State.viewOffset + CRAFTING_ORDERS_MAX_ROWS;
    if State.recipes[State.viewOffset + 1] then
        State.selected = State.viewOffset + 1;
    end
    RefreshList();
end

local RequestRecipesPage;

local function ReloadRecipes()
    if State.mode == MODE_HANDIN or State.service == 3 then
        return;
    end
    State.recipes = {};
    State.selected = 0;
    State.viewOffset = 0;
    State.pendingPages = true;
    SetStatus("Loading...");
    UpdateListOptions();
    RequestRecipesPage(0);
end

function CraftingOrders_ToggleFilter()
    if State.mode == MODE_HANDIN or State.service == 3 then
        return;
    end
    if State.listFilter == "MAKEABLE" then
        State.listFilter = "";
    else
        State.listFilter = "MAKEABLE";
    end
    ReloadRecipes();
end

function CraftingOrders_CycleTier()
    if State.mode == MODE_HANDIN or State.service == 3 then
        return;
    end
    State.listTier = State.listTier + 1;
    if State.listTier > 4 then
        State.listTier = 0;
    end
    ReloadRecipes();
end

function CraftingOrders_OnHide()
    SendCO("CLOSE", "");
    State.mode = MODE_BROWSE;
    State.pendingPages = false;
end

function CraftingOrders_Craft()
    local recipe = State.recipes[State.selected];
    if not recipe then
        SetStatus("Select an item first.");
        return;
    end
    if State.mode == MODE_HANDIN or recipe.kind == "handin" then
        SendCO("HANDIN", tostring(recipe.itemGuid or recipe.spellId));
        return;
    end
    if State.mode == MODE_ENCHANT_TARGETS or recipe.kind == "enchant_target" then
        State.pendingEnchant = recipe;
        if StaticPopupDialogs and StaticPopup_Show then
            StaticPopup_Show("CRAFTING_ORDERS_ENCHANT_CONFIRM", recipe.name);
        else
            CraftingOrders_ConfirmEnchant();
        end
        return;
    end
    if State.service == 3 then
        SendCO("DISENCHANT", tostring(recipe.bag).."\t"..tostring(recipe.slot));
        return;
    end
    if recipe.available < 1 or State.quantity < 1 then
        if recipe.cooldown and recipe.cooldown > 0 then
            SetStatus("This recipe is on cooldown.");
        else
            SetStatus("You do not have enough materials for one item.");
        end
        return;
    end
    if State.service == 2 and recipe.itemId == 0 then
        SendCO("REQUEST_ENCHANT_TARGETS", tostring(recipe.spellId));
        return;
    end
    SendCO("CRAFT", tostring(recipe.spellId).."\t"..tostring(State.quantity));
end

function CraftingOrders_ConfirmEnchant()
    local recipe = State.pendingEnchant;
    State.pendingEnchant = nil;
    if not recipe then
        return;
    end
    SendCO("ENCHANT", tostring(recipe.spellId).."\t"..tostring(recipe.bag).."\t"..tostring(recipe.slot));
end

local function ShowFrame(title)
    CraftingOrdersFrameTitle:SetText(title or "Crafting Orders");
    CraftingOrdersFrame:Show();
end

RequestRecipesPage = function(page)
    State.pendingPages = true;
    UpdateListOptions();
    SendCO("REQUEST_RECIPES", State.listFilter.."\t"..tostring(State.listTier).."\t"..tostring(page));
end

local function AppendParsed(entries, parser)
    local i;
    local added = 0;
    for i = 1, tlen(entries) do
        if entries[i] ~= "" then
            local recipe = parser(entries[i]);
            if recipe then
                table.insert(State.recipes, recipe);
                added = added + 1;
            end
        end
    end
    return added;
end

local function FinishList(status)
    if tlen(State.recipes) == 0 then
        State.selected = 0;
        SetStatus(status or "No recipes available.");
    else
        if State.selected == 0 then
            State.selected = 1;
        end
        SetStatus(tlen(State.recipes).." recipes.");
    end
    State.viewOffset = 0;
    RefreshList();
    ShowFrame(State.professionName);
end

local function HandleComplete(opcode, payload, page, totalPages)
    page = page or 0;
    totalPages = totalPages or 1;

    if opcode == "INIT" then
        local parts = split(payload, "\t");
        State.professionId = tonumber(parts[1] or "0") or 0;
        State.professionName = parts[2] or "Crafting";
        State.service = tonumber(parts[3] or "0") or 0;
        State.mode = tonumber(parts[4] or "0") or 0;
        State.recipes = {};
        State.selected = 0;
        State.listFilter = "";
        State.listTier = 0;
        State.pendingPages = false;
        State.viewOffset = 0;
        State.quantity = 0;
        ShowFrame(State.professionName);
        if State.mode == MODE_HANDIN then
            SetActionText("Hand in");
            SendCO("REQUEST_HANDIN", "0");
        elseif State.service == 3 then
            SetActionText("Disenchant");
            SendCO("REQUEST_DISENCHANT_ITEMS", "0");
        else
            SetActionText("Create");
            RequestRecipesPage(0);
        end
        SetStatus("Loading...");
        return;
    end

    if opcode == "RECIPES" then
        if page == 0 then
            State.recipes = {};
            State.selected = 0;
            State.viewOffset = 0;
        end
        AppendParsed(splitEscaped(payload, "|"), ParseRecipe);
        if page + 1 < totalPages then
            State.pendingPages = true;
            if State.service == 3 then
                SendCO("REQUEST_DISENCHANT_ITEMS", tostring(page + 1));
            else
                RequestRecipesPage(page + 1);
            end
            SetStatus("Loading page "..tostring(page + 2).."/"..tostring(totalPages).."...");
            return;
        end
        State.pendingPages = false;
        if State.service == 3 then
            SetActionText("Disenchant");
        else
            State.mode = MODE_BROWSE;
            SetActionText("Create");
        end
        FinishList("No recipes available.");
        return;
    end

    if opcode == "HANDIN" then
        if page == 0 then
            State.recipes = {};
            State.selected = 0;
            State.viewOffset = 0;
        end
        State.mode = MODE_HANDIN;
        SetActionText("Hand in");
        AppendParsed(splitEscaped(payload, "|"), ParseHandIn);
        if page + 1 < totalPages then
            State.pendingPages = true;
            SendCO("REQUEST_HANDIN", tostring(page + 1));
            SetStatus("Loading page "..tostring(page + 2).."/"..tostring(totalPages).."...");
            return;
        end
        State.pendingPages = false;
        FinishList("No formulas to hand in.");
        return;
    end

    if opcode == "CRAFT_RESULT" or opcode == "ENCHANT_RESULT" or opcode == "DISENCHANT_RESULT" then
        SetStatus(payload);
        State.mode = MODE_BROWSE;
        if State.service == 3 then
            SendCO("REQUEST_DISENCHANT_ITEMS", "0");
        else
            SetActionText("Create");
            RequestRecipesPage(0);
        end
        return;
    end

    if opcode == "HANDIN_RESULT" then
        SetStatus(payload);
        State.mode = MODE_BROWSE;
        SetActionText("Create");
        RequestRecipesPage(0);
        return;
    end

    if opcode == "ENCHANT_TARGETS" then
        local parts = splitEscaped(payload, "|");
        if tlen(parts) < 2 then
            SetStatus("No matching item.");
            return;
        end
        local spellId = tonumber(parts[1] or "0") or 0;
        State.mode = MODE_ENCHANT_TARGETS;
        State.recipes = {};
        State.selected = 0;
        SetActionText("Enchant");
        local i;
        for i = 2, tlen(parts) do
            if parts[i] ~= "" then
                table.insert(State.recipes, ParseEnchantTarget(parts[i], spellId));
            end
        end
        if tlen(State.recipes) == 0 then
            SetStatus("No matching item.");
            State.mode = MODE_BROWSE;
            SetActionText("Create");
            return;
        end
        State.selected = 1;
        RefreshList();
        SetStatus("Select an item to enchant.");
        ShowFrame(State.professionName);
        return;
    end

    if opcode == "ERROR" then
        State.pendingPages = false;
        UpdateListOptions();
        SetStatus(payload);
        if payload == "protocol version mismatch" then
            CraftingOrdersFrame:Hide();
            DEFAULT_CHAT_FRAME:AddMessage("Crafting Orders: addon protocol does not match the server.");
        end
    end
end

function CraftingOrders_OnEvent()
    if event == "PLAYER_LOGOUT" then
        if CraftingOrdersFrame:IsShown() then
            CraftingOrdersFrame:Hide();
        end
        return;
    end
    if event ~= "CHAT_MSG_ADDON" then
        return;
    end
    if arg1 ~= "CO" then
        return;
    end

    local parts = split(arg2, "\t");
    if tlen(parts) < 7 then
        return;
    end
    local version = tonumber(parts[1] or "0") or 0;
    local reqId = tonumber(parts[2] or "0") or 0;
    local part = tonumber(parts[3] or "0") or 0;
    local total = tonumber(parts[4] or "0") or 0;
    local opcode = parts[5] or "";
    local page = tonumber(parts[6] or "0") or 0;
    local totalPages = tonumber(parts[7] or "1") or 1;
    local payload = "";
    local i;
    for i = 8, tlen(parts) do
        if i > 8 then
            payload = payload.."\t";
        end
        payload = payload..parts[i];
    end

    if version ~= CRAFTING_ORDERS_PROTOCOL then
        HandleComplete("ERROR", "protocol version mismatch", 0, 1);
        return;
    end

    if total <= 1 then
        HandleComplete(opcode, payload, page, totalPages);
        return;
    end

    if State.chunkReq ~= reqId or State.chunkOpcode ~= opcode then
        State.chunks = {};
        State.chunkReq = reqId;
        State.chunkOpcode = opcode;
        State.chunkTotal = total;
    end
    State.chunks[part] = payload or "";
    local have = 0;
    for i = 1, total do
        if State.chunks[i] then
            have = have + 1;
        end
    end
    if have >= total then
        local assembled = "";
        for i = 1, total do
            assembled = assembled..(State.chunks[i] or "");
        end
        State.chunks = {};
        HandleComplete(opcode, assembled, page, totalPages);
    end
end
