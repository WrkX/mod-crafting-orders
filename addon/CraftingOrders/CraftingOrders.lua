-- Vanilla/Turtle 1.12 Crafting Orders UI. Lua 5.0: no #table, use this/event/argN.

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
    local me = UnitName("player");
    SendAddonMessage("CO", body, "WHISPER", me);
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
        bag = tonumber(head[12] or "0") or 0,
        slot = tonumber(head[13] or "0") or 0,
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
            local recipe = State.recipes[i];
            if recipe then
                btn:SetText(recipe.name);
                btn:Show();
            else
                btn:SetText("");
                btn:Hide();
            end
        end
    end
    CraftingOrders_Select(State.selected);
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
        return;
    end
    if recipe.kind == "handin" then
        detail:SetText(recipe.name.."\nHand in this formula to unlock the recipe.");
        return;
    end
    if recipe.kind == "enchant_target" then
        detail:SetText(recipe.name.."\nSelect this item, then click Enchant.");
        return;
    end
    local lines = recipe.name.."\nSkill: "..recipe.reqRank.."\nFee: "..recipe.fee.."c\nCan make: "..recipe.available.."\n";
    local i;
    for i = 1, tlen(recipe.materials) do
        local mat = recipe.materials[i];
        lines = lines.."\n"..mat.count.."x "..mat.name.." ("..mat.have..")";
    end
    detail:SetText(lines);
end

function CraftingOrders_OnLoad()
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
        SendCO("ENCHANT", tostring(recipe.spellId).."\t"..tostring(recipe.bag).."\t"..tostring(recipe.slot));
        return;
    end
    if State.service == 3 then
        SendCO("DISENCHANT", tostring(recipe.bag).."\t"..tostring(recipe.slot));
        return;
    end
    if State.service == 2 and recipe.itemId == 0 then
        SendCO("REQUEST_ENCHANT_TARGETS", tostring(recipe.spellId));
        return;
    end
    SendCO("CRAFT", tostring(recipe.spellId).."\t1");
end

local function ShowFrame(title)
    CraftingOrdersFrameTitle:SetText(title or "Crafting Orders");
    CraftingOrdersFrame:Show();
end

local function RequestRecipesPage(page)
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
