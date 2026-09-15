#include "CraftingOrders.h"
#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace
{
    std::string GetItemSubClassName(ItemPrototype const* item)
    {
        if (!item)
            return "Other";
        switch (item->Class)
        {
            case ITEM_CLASS_WEAPON: return "Weapon";
            case ITEM_CLASS_ARMOR:
                switch (item->SubClass)
                {
                    case ITEM_SUBCLASS_ARMOR_CLOTH: return "Cloth";
                    case ITEM_SUBCLASS_ARMOR_LEATHER: return "Leather";
                    case ITEM_SUBCLASS_ARMOR_MAIL: return "Mail";
                    case ITEM_SUBCLASS_ARMOR_PLATE: return "Plate";
                    case ITEM_SUBCLASS_ARMOR_SHIELD: return "Shields";
                    default: return "Armor";
                }
            case ITEM_CLASS_TRADE_GOODS: return "Trade Goods";
            case ITEM_CLASS_CONSUMABLE: return "Consumables";
            default: return "Other";
        }
    }

    std::string GetInventorySlotName(ItemPrototype const* item)
    {
        if (!item)
            return "";
        switch (item->InventoryType)
        {
            case INVTYPE_HEAD: return "Head";
            case INVTYPE_NECK: return "Neck";
            case INVTYPE_SHOULDERS: return "Shoulders";
            case INVTYPE_BODY: return "Shirt";
            case INVTYPE_CHEST:
            case INVTYPE_ROBE: return "Chest";
            case INVTYPE_WAIST: return "Waist";
            case INVTYPE_LEGS: return "Legs";
            case INVTYPE_FEET: return "Boots";
            case INVTYPE_WRISTS: return "Wrists";
            case INVTYPE_HANDS: return "Hands";
            case INVTYPE_FINGER: return "Rings";
            case INVTYPE_TRINKET: return "Trinkets";
            case INVTYPE_WEAPON: return "One-Hand";
            case INVTYPE_SHIELD: return "Shields";
            case INVTYPE_2HWEAPON: return "Two-Hand";
            case INVTYPE_CLOAK: return "Cloaks";
            case INVTYPE_WEAPONMAINHAND: return "Main Hand";
            case INVTYPE_WEAPONOFFHAND: return "Off Hand";
            case INVTYPE_HOLDABLE: return "Held In Off-hand";
            case INVTYPE_RANGED:
            case INVTYPE_RANGEDRIGHT: return "Ranged";
            default: return "";
        }
    }

    uint32 RemainingCooldown(Player* player, uint32 spellId)
    {
        return sCraftingOrders.GetCooldownRemaining(player, spellId);
    }

    uint32 ConsumableItemCount(Player* player, uint32 itemId)
    {
        uint64 count = 0;
        player->ApplyForAllItems([&](Item* item)
        {
            Bag* bag = item ? item->ToBag() : nullptr;
            if (item && item->GetEntry() == itemId && !item->IsInTrade() && (!bag || bag->IsEmpty()))
                count += item->GetCount();
        });
        return count > std::numeric_limits<uint32>::max()
            ? std::numeric_limits<uint32>::max()
            : uint32(count);
    }
}

void CraftingOrders::SendAddon(Player* player, uint32 requestId, std::string const& opcode, std::string const& payload, uint32 page, uint32 totalPages)
{
    if (!player)
        return;

    auto sendOne = [&](std::string const& sendOpcode, std::string const& bodyPayload, uint32 sendPage, uint32 sendTotalPages)
    {
        CraftingOrdersDomain::ChunkResult chunked = CraftingOrdersDomain::ChunkPayload(bodyPayload);
        if (chunked.overflow || chunked.recordTooLarge || chunked.chunks.empty())
        {
            std::string error = chunked.recordTooLarge ? "response record too large" : "response too large";
            std::string body = CraftingOrdersDomain::FormatProtocolHeader(
                CraftingOrdersDomain::ADDON_PROTOCOL_VERSION, requestId, 1, 1, "ERROR", 0, 1);
            body += "\t" + error;
            player->SendAddonMessage("CO", body);
            return;
        }

        uint32 total = uint32(chunked.chunks.size());
        if (total == 0)
            total = 1;
        for (uint32 part = 0; part < total; ++part)
        {
            std::string body = CraftingOrdersDomain::FormatProtocolHeader(
                CraftingOrdersDomain::ADDON_PROTOCOL_VERSION, requestId, part + 1, total, sendOpcode, sendPage, sendTotalPages);
            if (part < chunked.chunks.size() && !chunked.chunks[part].empty())
                body += "\t" + chunked.chunks[part];
            player->SendAddonMessage("CO", body);
        }
    };

    sendOne(opcode, payload, page, totalPages);
}

std::vector<std::string> CraftingOrders::BuildRecipeRecords(Player* player, uint32 professionId,
    std::string const& filter, uint32 tier, uint32 maxSkillRank) const
{
    std::vector<std::string> records;
    std::vector<RecipeData> recipes = GetAvailableRecipes(player, professionId, maxSkillRank);
    std::string lowerFilter = CraftingOrdersDomain::ToLowerCopy(filter);
    bool makeableOnly = lowerFilter == "makeable";

    for (RecipeData const& recipe : recipes)
    {
        if (tier > 0 && recipe.skillTier != tier)
            continue;
        uint32 const remaining = RemainingCooldown(player, recipe.spellId);
        if (makeableOnly)
        {
            std::string ignored;
            if (remaining || !ValidateMaterials(recipe, player, 1, ignored))
                continue;
        }
        else if (!filter.empty() && filter != "MAKEABLE")
        {
            ItemPrototype const* itemTmpl = sObjectMgr.GetItemPrototype(recipe.createdItemId);
            std::string itemName = itemTmpl ? itemTmpl->Name1 : recipe.displayName;
            if (CraftingOrdersDomain::ToLowerCopy(itemName).find(lowerFilter) == std::string::npos)
                continue;
        }

        ItemPrototype const* itemTmpl = sObjectMgr.GetItemPrototype(recipe.createdItemId);
        // A fresh 1.12 client can only populate GetItemInfo for item
        // prototypes that the server has marked as discovered.  Recipes shown
        // by this service are no longer secret, so allow the client's
        // throttled item-query queue to cache their names and icon data.
        if (itemTmpl)
            itemTmpl->Discovered = true;
        std::string itemName = itemTmpl ? itemTmpl->Name1 : recipe.displayName;
        uint32 goldFee = CalculateGoldFee(recipe);
        uint32 numAvailable = 0;
        std::string ignored;
        if (!remaining && ValidateMaterials(recipe, player, 1, ignored))
        {
            numAvailable = sCraftingOrdersConfig.MaxQuantity();
            for (CraftMaterial const& mat : recipe.materials)
            {
                if (!mat.count)
                    continue;
                uint32 canMake = ConsumableItemCount(player, mat.itemId) / mat.count;
                if (canMake < numAvailable)
                    numAvailable = canMake;
            }
            if (goldFee > 0)
            {
                uint32 byGold = player->GetMoney() / goldFee;
                if (byGold < numAvailable)
                    numAvailable = byGold;
            }
        }

        std::ostringstream ss;
        ss << recipe.spellId << "," << recipe.createdItemId << ","
           << CraftingOrdersDomain::EscapeField(itemName) << ","
           << recipe.skillTier << "," << recipe.reqSkillRank << ","
           << goldFee << "," << recipe.createdItemCount << "," << numAvailable << ","
           << CraftingOrdersDomain::EscapeField(GetItemSubClassName(itemTmpl)) << ","
           << CraftingOrdersDomain::EscapeField(GetInventorySlotName(itemTmpl)) << ",,,"
           << remaining;
        for (CraftMaterial const& mat : recipe.materials)
        {
            ItemPrototype const* matTmpl = sObjectMgr.GetItemPrototype(mat.itemId);
            if (matTmpl)
                matTmpl->Discovered = true;
            std::string matName = matTmpl ? matTmpl->Name1 : "Unknown";
            ss << ";" << mat.itemId << "," << mat.count << ","
               << CraftingOrdersDomain::EscapeField(matName) << ",,"
               << ConsumableItemCount(player, mat.itemId);
        }
        records.push_back(ss.str());
    }
    return records;
}

std::string CraftingOrders::BuildRecipeDataMessage(Player* player, uint32 professionId, std::string const& filter, uint32 tier) const
{
    return CraftingOrdersDomain::JoinRecords(BuildRecipeRecords(player, professionId, filter, tier));
}

std::vector<std::string> CraftingOrders::BuildDisenchantRecords(Player* player) const
{
    std::vector<std::string> records;
    ForEachInventoryItem(player, [&](Item* item)
    {
        ItemPrototype const* proto = item->GetProto();
        if (!proto || proto->DisenchantID == 0 || (proto->Flags & ITEM_FLAG_NO_DISENCHANT))
            return;
        if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_QUEST_ITEM1 || proto->IsQuestItem)
            return;
        if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
            return;
        if (!LootTemplates_Disenchant.HaveLootFor(proto->DisenchantID))
            return;
        uint32 fee = CraftingOrdersDomain::FeeForItemLevel(
            sCraftingOrdersConfig.Fees().disenchantFees, proto->ItemLevel, sCraftingOrdersConfig.Fees().disenchantDefaultFee);
        std::ostringstream ss;
        ss << item->GetGUIDLow() << "," << item->GetEntry() << ","
           << CraftingOrdersDomain::EscapeField(proto->Name1) << ",1,0,"
           << fee << ",1,1,"
           << (proto->Class == ITEM_CLASS_WEAPON ? "Weapon" : "Armor") << ",,,"
           << uint32(item->GetBagSlot()) << "," << uint32(item->GetSlot());
        records.push_back(ss.str());
    });
    return records;
}

std::string CraftingOrders::BuildDisenchantDataMessage(Player* player) const
{
    return CraftingOrdersDomain::JoinRecords(BuildDisenchantRecords(player));
}

std::vector<std::string> CraftingOrders::BuildHandInRecords(Player* player, uint32 professionId) const
{
    std::vector<std::string> records;
    ForEachInventoryItem(player, [&](Item* item)
    {
        uint32 taught = 0;
        RecipeData const* recipe = ResolveRecipeItem(item->GetProto(), professionId, taught);
        if (!recipe || !recipe->requiresUnlock || HasPlayerRecipe(player, professionId, taught))
            return;
        if (ItemPrototype const* createdItem = sObjectMgr.GetItemPrototype(recipe->createdItemId))
            createdItem->Discovered = true;
        std::ostringstream ss;
        ss << item->GetGUIDLow() << "," << item->GetEntry() << ","
           << CraftingOrdersDomain::EscapeField(item->GetProto()->Name1) << ","
           << taught << "," << recipe->createdItemId;
        records.push_back(ss.str());
    });
    return records;
}

std::string CraftingOrders::BuildHandInDataMessage(Player* player, uint32 professionId) const
{
    return CraftingOrdersDomain::JoinRecords(BuildHandInRecords(player, professionId));
}

bool CraftingOrders::HandleAddonPacket(WorldSession* session, WorldPacket const& packet)
{
    if (!session || !Enabled())
        return true;
    if (packet.GetOpcode() != CMSG_MESSAGECHAT)
        return true;

    Player* player = session->GetPlayer();
    if (!player)
        return true;
    if (packet.size() > CraftingOrdersDomain::MAX_MESSAGE_SIZE + 32)
        return true;

    WorldPacket copy(packet);
    copy.rpos(0);
    uint32 type = 0;
    uint32 lang = 0;
    copy >> type >> lang;
    if (lang != LANG_ADDON)
        return true;
    if (type != CHAT_MSG_WHISPER && type != CHAT_MSG_GUILD && type != CHAT_MSG_PARTY)
        return true;

    std::string to;
    std::string msg;
    if (type == CHAT_MSG_WHISPER)
        copy >> to;
    copy >> msg;
    if (msg.size() > CraftingOrdersDomain::MAX_MESSAGE_SIZE)
        return true;

    size_t tab = msg.find('\t');
    if (tab == std::string::npos)
        return true;
    if (msg.substr(0, tab) != "CO")
        return true;

    CraftingOrdersDomain::ProtocolRequest req = CraftingOrdersDomain::ParseProtocolPayload(msg.substr(tab + 1));
    if (!req.valid)
        return false;

    if (req.version != CraftingOrdersDomain::ADDON_PROTOCOL_VERSION)
    {
        SendAddon(player, req.requestId, "ERROR", "protocol version mismatch");
        return false;
    }

    CraftingSession* craftSession = GetSession(player);
    if (!craftSession)
        return false;
    if (!RateLimit(*craftSession, GetNowMs()))
    {
        SendAddon(player, req.requestId, "ERROR", "rate limited");
        return false;
    }
    if (CraftingOrdersDomain::ReplayRejected(craftSession->lastRequestId, req.requestId))
    {
        SendAddon(player, req.requestId, "ERROR", "replay rejected");
        return false;
    }
    if (craftSession->busy)
    {
        SendAddon(player, req.requestId, "ERROR", "busy");
        return false;
    }
    craftSession->lastRequestId = req.requestId;
    craftSession->busy = true;

    auto finish = [&](std::string const& opcode, std::string const& payload)
    {
        craftSession->busy = false;
        SendAddon(player, req.requestId, opcode, payload);
    };

    std::string error;
    auto finishList = [&](std::string const& opcode, std::vector<std::string> const& records, uint32 page)
    {
        CraftingOrdersDomain::PagedRecords paged = CraftingOrdersDomain::PaginateRecords(records, page);
        craftSession->busy = false;
        if (paged.recordTooLarge)
        {
            SendAddon(player, req.requestId, "ERROR", "response record too large");
            return;
        }
        if (paged.pageOutOfRange)
        {
            SendAddon(player, req.requestId, "ERROR", "page out of range");
            return;
        }
        SendAddon(player, req.requestId, opcode, CraftingOrdersDomain::JoinRecords(paged.records), paged.page, paged.totalPages);
    };

    if (req.opcode == "CLOSE")
    {
        craftSession->busy = false;
        CloseSession(player);
        return false;
    }

    if (req.opcode == "REQUEST_RECIPES")
    {
        if (!ValidateSession(player, 0, error))
        {
            finish("ERROR", error);
            return false;
        }
        uint32 tier = 0;
        uint32 page = 0;
        std::string filter;
        if (!req.fields.empty())
            filter = req.fields[0];
        if (req.fields.size() >= 2)
            CraftingOrdersDomain::ParseU32(req.fields[1], tier);
        if (req.fields.size() >= 3)
            CraftingOrdersDomain::ParseU32(req.fields[2], page);
        finishList("RECIPES", BuildRecipeRecords(player, craftSession->professionId, filter, tier,
            craftSession->maxSkillRank), page);
        return false;
    }

    if (req.opcode == "REQUEST_DISENCHANT_ITEMS")
    {
        if (!ValidateSession(player, CraftingOrdersDomain::SERVICE_DISENCHANT, error))
        {
            finish("ERROR", error);
            return false;
        }
        uint32 page = 0;
        if (!req.fields.empty())
            CraftingOrdersDomain::ParseU32(req.fields[0], page);
        finishList("RECIPES", BuildDisenchantRecords(player), page);
        return false;
    }

    if (req.opcode == "REQUEST_HANDIN")
    {
        if (!ValidateSession(player, 0, error))
        {
            finish("ERROR", error);
            return false;
        }
        uint32 page = 0;
        if (!req.fields.empty())
            CraftingOrdersDomain::ParseU32(req.fields[0], page);
        finishList("HANDIN", BuildHandInRecords(player, craftSession->professionId), page);
        return false;
    }

    if (req.opcode == "CRAFT")
    {
        uint32 spellId = 0;
        uint32 quantity = 1;
        if (req.fields.empty() || !CraftingOrdersDomain::ParseU32(req.fields[0], spellId))
        {
            finish("CRAFT_RESULT", "FAIL\tbad spell");
            return false;
        }
        if (req.fields.size() >= 2 && !CraftingOrdersDomain::ParseU32(req.fields[1], quantity))
        {
            finish("CRAFT_RESULT", "FAIL\tbad quantity");
            return false;
        }
        std::string result;
        bool ok = Craft(player, spellId, quantity, result);
        finish("CRAFT_RESULT", std::string(ok ? "OK\t" : "FAIL\t") + result);
        return false;
    }

    if (req.opcode == "ENCHANT")
    {
        uint32 spellId = 0;
        uint32 bag = 0;
        uint32 slot = 0;
        if (req.fields.size() < 3 ||
            !CraftingOrdersDomain::ParseU32(req.fields[0], spellId) ||
            !CraftingOrdersDomain::ParseU32(req.fields[1], bag) ||
            !CraftingOrdersDomain::ParseU32(req.fields[2], slot))
        {
            finish("ENCHANT_RESULT", "FAIL\tbad target");
            return false;
        }
        std::string result;
        bool ok = Enchant(player, spellId, bag, slot, result);
        finish("ENCHANT_RESULT", std::string(ok ? "OK\t" : "FAIL\t") + result);
        return false;
    }

    if (req.opcode == "REQUEST_ENCHANT_TARGETS")
    {
        uint32 spellId = 0;
        if (req.fields.empty() || !CraftingOrdersDomain::ParseU32(req.fields[0], spellId))
        {
            finish("ENCHANT_RESULT", "FAIL\tbad spell");
            return false;
        }
        if (!ValidateSession(player, CraftingOrdersDomain::SERVICE_ENCHANT, error))
        {
            finish("ERROR", error);
            return false;
        }
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
        RecipeData const* recipe = GetRecipeForSpell(spellId);
        if (!spellInfo || !recipe || !recipe->isEnchantSpell)
        {
            finish("ENCHANT_RESULT", "FAIL\tEnchant not found");
            return false;
        }
        if (recipe->professionId != craftSession->professionId ||
            !CraftingOrdersDomain::RecipeWithinSkillCap(recipe->reqSkillRank, craftSession->maxSkillRank) ||
            !PlayerCanUseRecipe(player, *recipe))
        {
            finish("ENCHANT_RESULT", "FAIL\tEnchant not found");
            return false;
        }
        std::ostringstream response;
        response << spellId;
        uint32 matchCount = 0;
        auto append = [&](Item* item, uint8 bag, uint8 slot, std::string const& location)
        {
            if (!item || !item->IsFitToSpellRequirements(spellInfo))
                return;
            response << "|" << uint32(bag) << "^" << uint32(slot) << "^"
                     << CraftingOrdersDomain::EscapeField(location) << "^"
                     << item->GetEntry() << "^" << CraftingOrdersDomain::EscapeField(item->GetProto()->Name1);
            ++matchCount;
        };
        for (uint32 equipSlot : GetEnchantEquipmentSlots())
            append(player->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(equipSlot)), INVENTORY_SLOT_BAG_0, uint8(equipSlot),
                "Equipped: " + GetSlotName(equipSlot));
        ForEachInventoryItem(player, [&](Item* item)
        {
            append(item, item->GetBagSlot(), item->GetSlot(), "Bags");
        });
        if (!matchCount)
        {
            finish("ENCHANT_RESULT", "FAIL\tYou do not have a matching item for that enchant");
            return false;
        }
        finish("ENCHANT_TARGETS", response.str());
        return false;
    }

    if (req.opcode == "DISENCHANT")
    {
        uint32 bag = 0;
        uint32 slot = 0;
        if (req.fields.size() < 2 ||
            !CraftingOrdersDomain::ParseU32(req.fields[0], bag) ||
            !CraftingOrdersDomain::ParseU32(req.fields[1], slot))
        {
            finish("DISENCHANT_RESULT", "FAIL\tbad target");
            return false;
        }
        std::string result;
        bool ok = Disenchant(player, bag, slot, result);
        finish("DISENCHANT_RESULT", std::string(ok ? "OK\t" : "FAIL\t") + result);
        return false;
    }

    if (req.opcode == "HANDIN")
    {
        uint32 itemGuid = 0;
        if (req.fields.empty() || !CraftingOrdersDomain::ParseU32(
                req.fields[0], itemGuid, std::numeric_limits<uint32>::max()))
        {
            finish("HANDIN_RESULT", "FAIL\tbad item");
            return false;
        }
        std::string result;
        bool ok = HandIn(player, itemGuid, result);
        finish("HANDIN_RESULT", std::string(ok ? "OK\t" : "FAIL\t") + result);
        return false;
    }

    craftSession->busy = false;
    return false;
}
