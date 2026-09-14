#include "CraftingOrders.h"
#include "Bag.h"
#include "Config/Config.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "LootMgr.h"
#include "Mail.h"
#include "Map.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <algorithm>
#include <memory>
#include <sstream>

namespace
{
// All deadlines in this module are short relative to the uint32 millisecond
// clock's wrap period.  Signed subtraction keeps comparisons correct when the
// clock wraps instead of treating a wrapped deadline as being far in the
// future.
bool DeadlineReached(uint32 now, uint32 deadline)
{
    return int32(now - deadline) >= 0;
}

bool IsConsumableReagent(Item* item)
{
    if (!item || item->IsInTrade())
        return false;
    Bag* bag = item->ToBag();
    return !bag || bag->IsEmpty();
}

void NoteProfessionCap(std::map<uint32, uint32>& professionCaps, uint32 professionId, uint32 maxSkillRank)
{
    if (!CraftingOrdersDomain::IsSupportedProfession(professionId) || !maxSkillRank)
        return;
    uint32& current = professionCaps[professionId];
    current = std::max(current, std::min(maxSkillRank, CraftingOrdersDomain::ARTISAN_SKILL_CAP));
}

void NoteProfessionSpellCaps(SpellEntry const* spellInfo, std::map<uint32, uint32>& professionCaps,
    uint32 depth = 0)
{
    if (!spellInfo || depth > 4)
        return;

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_SKILL)
        {
            int32 const step = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));
            if (step > 0)
            {
                uint32 const artisanStep = CraftingOrdersDomain::ARTISAN_SKILL_CAP / 75;
                NoteProfessionCap(professionCaps, uint32(spellInfo->EffectMiscValue[i]),
                    std::min(uint32(step), artisanStep) * 75);
            }
        }
        else if (spellInfo->Effect[i] == SPELL_EFFECT_LEARN_SPELL && spellInfo->EffectTriggerSpell[i])
            NoteProfessionSpellCaps(sSpellMgr.GetSpellEntry(spellInfo->EffectTriggerSpell[i]), professionCaps, depth + 1);
    }
}

void NoteTrainerCapabilities(TrainerSpellData const* data, std::map<uint32, uint32>& professionCaps)
{
    if (!data)
        return;

    for (auto const& entry : data->spellList)
    {
        TrainerSpell const& trainerSpell = entry.second;
        if (CraftingOrdersDomain::IsSupportedProfession(trainerSpell.reqSkill))
        {
            uint32 const tier = CraftingOrdersDomain::GetSkillTier(trainerSpell.reqSkillValue);
            if (tier)
                NoteProfessionCap(professionCaps, trainerSpell.reqSkill, tier * 75);
        }
        NoteProfessionSpellCaps(sSpellMgr.GetSpellEntry(trainerSpell.spell), professionCaps);
    }
}
}

CraftingOrders& CraftingOrders::Instance()
{
    static CraftingOrders instance;
    return instance;
}

bool CraftingOrders::Enabled() const
{
    return sCraftingOrdersConfig.Enabled() && _loaded && !_disabledForData;
}

NpcBinding const* CraftingOrders::GetNpcBinding(uint32 creatureEntry) const
{
    auto it = _npcBindings.find(creatureEntry);
    if (it == _npcBindings.end() || !it->second.available)
        return nullptr;
    return &it->second;
}

bool CraftingOrders::ResolveNpcBinding(Creature const* creature, NpcBinding& binding) const
{
    if (!creature)
        return false;

    // Explicit bindings (the legacy crafting-order NPCs) take precedence and
    // retain their configured service exactly as before.
    if (NpcBinding const* configured = GetNpcBinding(creature->GetEntry()))
    {
        binding = *configured;
        if (creature->IsTrainer() && creature->GetCreatureInfo() &&
            creature->GetCreatureInfo()->trainer_type == TRAINER_TYPE_TRADESKILLS)
        {
            std::map<uint32, uint32> professionCaps;
            NoteTrainerCapabilities(creature->GetTrainerSpells(), professionCaps);
            NoteTrainerCapabilities(creature->GetTrainerTemplateSpells(), professionCaps);
            auto capability = professionCaps.find(binding.professionId);
            if (capability != professionCaps.end())
                binding.maxSkillRank = capability->second;
        }
        return true;
    }

    // Profession trainers are discovered from the core's already loaded
    // trainer data.  No creature_template or script-name mutation is needed.
    if (!creature->IsTrainer() || !creature->GetCreatureInfo() ||
        creature->GetCreatureInfo()->trainer_type != TRAINER_TYPE_TRADESKILLS)
        return false;

    std::map<uint32, uint32> professionCaps;
    NoteTrainerCapabilities(creature->GetTrainerSpells(), professionCaps);
    NoteTrainerCapabilities(creature->GetTrainerTemplateSpells(), professionCaps);

    // Prefer the lowest supported profession deterministically, but only when
    // it actually has valid trainer recipes. This avoids adding an empty
    // module menu to a trainer whose rows happen to use a supported skillline
    // while all of its spells were filtered out during recipe loading.
    uint32 professionId = CraftingOrdersDomain::PROF_NONE;
    uint32 maxSkillRank = 0;
    for (auto const& capability : professionCaps)
    {
        uint32 const candidate = capability.first;
        auto recipes = _trainerRecipes.find(candidate);
        if (recipes != _trainerRecipes.end() && !recipes->second.empty())
        {
            professionId = candidate;
            maxSkillRank = capability.second;
            break;
        }
    }

    // Some cores/data sets only expose the profession-learning spell on the
    // creature template.  Its SKILL effect is an equally authoritative
    // fallback when no recipe row is present on that trainer.
    if (!professionId && creature->GetCreatureInfo()->trainer_spell)
    {
        std::map<uint32, uint32> fallbackCaps;
        NoteProfessionSpellCaps(sSpellMgr.GetSpellEntry(creature->GetCreatureInfo()->trainer_spell), fallbackCaps);
        for (auto const& capability : fallbackCaps)
        {
            uint32 const candidate = capability.first;
            auto recipes = _trainerRecipes.find(candidate);
            if (recipes != _trainerRecipes.end() && !recipes->second.empty())
            {
                professionId = candidate;
                maxSkillRank = capability.second;
                break;
            }
        }
    }

    if (!professionId)
        return false;

    binding.entry = creature->GetEntry();
    binding.professionId = professionId;
    binding.maxSkillRank = maxSkillRank;
    binding.service = professionId == CraftingOrdersDomain::PROF_ENCHANTING
        ? CraftingOrdersDomain::SERVICE_ENCHANT
        : CraftingOrdersDomain::SERVICE_CRAFT;
    binding.scriptName.clear();
    binding.available = true;
    return true;
}

RecipeData const* CraftingOrders::GetRecipeForSpell(uint32 spellId) const
{
    auto it = _spellRecipeMap.find(spellId);
    return it != _spellRecipeMap.end() ? &it->second : nullptr;
}

uint32 CraftingOrders::GetNowMs() const
{
    return _elapsedMs;
}

void CraftingOrders::Load()
{
    // A reload must not retain sessions created under the previous config or
    // NPC bindings.  In particular, a disable/re-enable cycle must require a
    // fresh NPC interaction.
    _sessions.clear();
    _loaded = false;
    _disabledForData = false;
    sCraftingOrdersConfig.Load();
    if (!sCraftingOrdersConfig.Enabled())
    {
        sLog.outString("[mod-crafting-orders] Disabled by configuration.");
        return;
    }

    LoadNpcBindings();
    if (_disabledForData)
        return;

    LoadRecipes();
    CleanupExpiredCooldowns();
    _loaded = true;
    _nextCleanupMs = _elapsedMs + 60000;
}

void CraftingOrders::Update(uint32 diff)
{
    if (!Enabled())
        return;

    _elapsedMs += diff;
    if (DeadlineReached(_elapsedMs, _nextCleanupMs))
    {
        CleanupExpiredCooldowns();
        _nextCleanupMs = _elapsedMs + 60000;
    }

    for (auto it = _sessions.begin(); it != _sessions.end();)
    {
        CraftingSession const& session = it->second;
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, session.playerGuid));
        if (DeadlineReached(_elapsedMs, session.expiresMs))
            it = _sessions.erase(it);
        else if (!player || !ResolveSessionCreature(player, session))
        {
            if (player)
                SendAddon(player, 0, "CLOSE_UI", "");
            it = _sessions.erase(it);
        }
        else
            ++it;
    }
}

uint32 CraftingOrders::ResolveCraftSpell(uint32 trainerSpellId) const
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(trainerSpellId);
    if (!spellInfo)
        return 0;

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_LEARN_SPELL && spellInfo->EffectTriggerSpell[i])
            return spellInfo->EffectTriggerSpell[i];
    }
    return trainerSpellId;
}

bool CraftingOrders::IsEnchantmentSpell(SpellEntry const* spellInfo, uint32* enchantId, bool* permanent) const
{
    if (!spellInfo)
        return false;
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_ENCHANT_ITEM ||
            spellInfo->Effect[i] == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY)
        {
            if (enchantId)
                *enchantId = uint32(spellInfo->EffectMiscValue[i] < 0 ? 0 : spellInfo->EffectMiscValue[i]);
            if (permanent)
                *permanent = spellInfo->Effect[i] == SPELL_EFFECT_ENCHANT_ITEM;
            return true;
        }
    }
    return false;
}

bool CraftingOrders::MakeRecipeData(uint32 spellId, uint32 skillLine, uint32 skillRank, uint32 reqLevel, RecipeData& rd) const
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    if (!spellInfo)
        return false;
    if (skillRank == 0 || skillRank > CraftingOrdersDomain::ARTISAN_SKILL_CAP)
        return false;
    if (!CraftingOrdersDomain::RecipeAllowed(spellId, sCraftingOrdersConfig.AllowList(), sCraftingOrdersConfig.DenyList()))
        return false;

    std::string name = spellInfo->SpellName[0];
    std::string lower = CraftingOrdersDomain::ToLowerCopy(name);
    if (lower.find("test") != std::string::npos || lower.find("debug") != std::string::npos)
        return false;

    uint32 createdItemId = 0;
    uint32 createdItemCount = 1;
    uint32 enchantId = 0;
    bool permanentEnchant = true;
    bool isEnchant = IsEnchantmentSpell(spellInfo, &enchantId, &permanentEnchant);

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && spellInfo->EffectItemType[i])
        {
            createdItemId = spellInfo->EffectItemType[i];
            int32 base = spellInfo->EffectBasePoints[i] + 1;
            createdItemCount = base > 0 ? uint32(base) : 1;
            break;
        }
    }

    if (!createdItemId && !isEnchant)
        return false;
    if (isEnchant && enchantId == 0)
        return false;

    rd = RecipeData();
    rd.spellId = spellId;
    rd.professionId = skillLine;
    rd.createdItemId = createdItemId;
    rd.createdItemCount = createdItemCount;
    rd.skillTier = CraftingOrdersDomain::GetSkillTier(skillRank);
    rd.reqSkillRank = skillRank;
    rd.reqLevel = reqLevel;
    rd.spellCooldownSecs = (spellInfo->RecoveryTime ? spellInfo->RecoveryTime : spellInfo->CategoryRecoveryTime) / 1000;
    rd.spellCategory = spellInfo->Category;
    rd.goldFeeMultiplier = sCraftingOrdersConfig.RecipeMultiplier(skillLine, spellId);
    rd.isEnchantSpell = isEnchant;
    rd.isPermanentEnchant = permanentEnchant;
    rd.enchantId = enchantId;
    rd.displayName = name;

    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->Reagent[i] > 0 && spellInfo->ReagentCount[i] > 0)
        {
            uint32 const itemId = uint32(spellInfo->Reagent[i]);
            uint32 const count = spellInfo->ReagentCount[i];
            auto existing = std::find_if(rd.materials.begin(), rd.materials.end(), [itemId](CraftMaterial const& mat)
            {
                return mat.itemId == itemId;
            });
            if (existing == rd.materials.end())
                rd.materials.push_back({ itemId, count });
            else if (!CraftingOrdersDomain::CheckedAddU32(existing->count, count, existing->count))
                return false;
        }
    }
    return rd.goldFeeMultiplier > 0.0f;
}

void CraftingOrders::LoadRecipes()
{
    _trainerRecipes.clear();
    _spellRecipeMap.clear();

    uint32 profIds[] = {
        CraftingOrdersDomain::PROF_BLACKSMITHING,
        CraftingOrdersDomain::PROF_LEATHERWORKING,
        CraftingOrdersDomain::PROF_ALCHEMY,
        CraftingOrdersDomain::PROF_TAILORING,
        CraftingOrdersDomain::PROF_ENGINEERING,
        CraftingOrdersDomain::PROF_ENCHANTING,
        CraftingOrdersDomain::PROF_JEWELCRAFTING
    };

    std::string profCsv;
    for (uint32 pid : profIds)
    {
        if (!profCsv.empty())
            profCsv += ",";
        profCsv += std::to_string(pid);
    }

    uint32 loaded = 0;
    uint32 skipped = 0;
    std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
        "SELECT DISTINCT spell, reqskill, reqskillvalue, reqlevel FROM npc_trainer "
        "WHERE reqskill IN (%s) ORDER BY reqskill, reqskillvalue, spell", profCsv.c_str()));
    if (!result)
        sLog.outError("[mod-crafting-orders] No npc_trainer rows found for supported professions.");
    else
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 trainerSpell = fields[0].GetUInt32();
            uint32 skillLine = fields[1].GetUInt32();
            uint32 skillRank = fields[2].GetUInt32();
            uint32 reqLevel = fields[3].GetUInt32();
            uint32 craftSpell = ResolveCraftSpell(trainerSpell);
            if (!craftSpell)
            {
                ++skipped;
                continue;
            }
            if (_spellRecipeMap.find(craftSpell) != _spellRecipeMap.end())
                continue;

            RecipeData rd;
            if (!MakeRecipeData(craftSpell, skillLine, skillRank, reqLevel, rd))
            {
                ++skipped;
                continue;
            }
            rd.source = CraftingOrdersDomain::RECIPE_SOURCE_TRAINER;
            rd.requiresUnlock = false;
            _trainerRecipes[skillLine].push_back(rd);
            _spellRecipeMap[craftSpell] = rd;
            ++loaded;
        } while (result->NextRow());
    }

    uint32 bonusLoaded = 0;
    std::unique_ptr<QueryResult> itemRecipes(WorldDatabase.PQuery(
        "SELECT required_skill, required_skill_rank, required_level, "
        "spellid_1, spellid_2, spellid_3, spellid_4, spellid_5 "
        "FROM item_template WHERE class = 9 AND required_skill IN (%s)", profCsv.c_str()));
    if (itemRecipes)
    {
        do
        {
            Field* fields = itemRecipes->Fetch();
            uint32 skillLine = fields[0].GetUInt32();
            uint32 skillRank = fields[1].GetUInt32();
            uint32 reqLevel = fields[2].GetUInt32();
            for (uint8 i = 0; i < 5; ++i)
            {
                uint32 spellId = fields[3 + i].GetUInt32();
                if (!spellId)
                    continue;
                uint32 craftSpell = ResolveCraftSpell(spellId);
                if (!craftSpell || _spellRecipeMap.find(craftSpell) != _spellRecipeMap.end())
                    continue;
                RecipeData rd;
                if (!MakeRecipeData(craftSpell, skillLine, skillRank, reqLevel, rd))
                    continue;
                rd.source = CraftingOrdersDomain::RECIPE_SOURCE_FORMULA;
                rd.requiresUnlock = true;
                _spellRecipeMap[craftSpell] = rd;
                ++bonusLoaded;
            }
        } while (itemRecipes->NextRow());
    }

    for (auto& pair : _trainerRecipes)
    {
        std::sort(pair.second.begin(), pair.second.end(), [](RecipeData const& a, RecipeData const& b) {
            return a.reqSkillRank < b.reqSkillRank;
        });
        sLog.outString("[mod-crafting-orders] %s: %u trainer recipes",
            CraftingOrdersDomain::ProfessionName(pair.first), uint32(pair.second.size()));
    }

    if (_spellRecipeMap.empty())
    {
        sLog.outError("[mod-crafting-orders] No valid recipes loaded; crafting service disabled.");
        _disabledForData = true;
        return;
    }

    sLog.outString("[mod-crafting-orders] Loaded %u trainer recipes and %u extra hand-in recipes (%u skipped).",
        loaded, bonusLoaded, skipped);
}

std::vector<RecipeData> CraftingOrders::GetAvailableRecipes(Player* player, uint32 professionId,
    uint32 maxSkillRank) const
{
    std::vector<RecipeData> available;
    auto it = _trainerRecipes.find(professionId);
    if (it != _trainerRecipes.end())
    {
        for (RecipeData const& recipe : it->second)
        {
            if (recipe.goldFeeMultiplier > 0.0f &&
                CraftingOrdersDomain::RecipeWithinSkillCap(recipe.reqSkillRank, maxSkillRank))
                available.push_back(recipe);
        }
    }

    if (!player || !player->GetSession())
        return available;

    std::unique_ptr<QueryResult> bonus(CharacterDatabase.PQuery(
        "SELECT spellId FROM crafting_order_recipes WHERE accountId = %u AND professionId = %u",
        player->GetSession()->GetAccountId(), professionId));
    if (bonus)
    {
        do
        {
            uint32 spellId = bonus->Fetch()[0].GetUInt32();
            auto spIt = _spellRecipeMap.find(spellId);
            if (spIt == _spellRecipeMap.end() || spIt->second.goldFeeMultiplier <= 0.0f ||
                !CraftingOrdersDomain::RecipeWithinSkillCap(spIt->second.reqSkillRank, maxSkillRank))
                continue;
            bool exists = false;
            for (RecipeData const& recipe : available)
            {
                if (recipe.spellId == spellId)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                available.push_back(spIt->second);
        } while (bonus->NextRow());
    }
    return available;
}

uint32 CraftingOrders::CalculateGoldFee(RecipeData const& recipe) const
{
    CraftingOrdersDomain::RecipeFeeInput input;
    input.professionId = recipe.professionId;
    input.createdItemId = recipe.createdItemId;
    input.reqSkillRank = recipe.reqSkillRank;
    input.skillTier = recipe.skillTier;
    input.recipeMultiplier = recipe.goldFeeMultiplier;
    input.isEnchantSpell = recipe.isEnchantSpell;
    if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(recipe.createdItemId))
    {
        input.createdItemSellPrice = proto->SellPrice;
        input.createdItemClass = proto->Class;
    }
    for (CraftMaterial const& mat : recipe.materials)
    {
        if (ItemPrototype const* matProto = sObjectMgr.GetItemPrototype(mat.itemId))
        {
            uint32 line = 0;
            if (CraftingOrdersDomain::CheckedMulU32(matProto->SellPrice, mat.count, line))
            {
                uint32 total = 0;
                if (CraftingOrdersDomain::CheckedAddU32(input.reagentSellTotal, line, total))
                    input.reagentSellTotal = total;
            }
        }
    }
    return CraftingOrdersDomain::CalculateRecipeFee(input, sCraftingOrdersConfig.Fees());
}

bool CraftingOrders::ValidateMaterials(RecipeData const& recipe, Player* player, uint32 quantity, std::string& error) const
{
    if (!player)
    {
        error = "invalid player";
        return false;
    }
    for (CraftMaterial const& mat : recipe.materials)
    {
        uint32 needed = 0;
        if (!CraftingOrdersDomain::TotalReagentCount(mat.count, quantity, needed))
        {
            error = "reagent total overflow";
            return false;
        }
        // Player::GetItemCount includes items currently in trade, while
        // DestroyItemCount deliberately skips those items.  Counting with
        // the former and consuming with the latter would allow a craft to
        // proceed without actually consuming all of its reagents.  Count
        // the same inventory set that the non-bank destruction path can use.
        uint64 available = 0;
        player->ApplyForAllItems([&](Item* item)
        {
            if (item && item->GetEntry() == mat.itemId && IsConsumableReagent(item))
                available += item->GetCount();
        });
        if (available < needed)
        {
            error = "You do not have the required materials";
            return false;
        }
    }
    return true;
}

bool CraftingOrders::PlayerCanUseRecipe(Player* player, RecipeData const& recipe) const
{
    if (!CraftingOrdersDomain::RecipeEntitled(recipe.source, HasPlayerRecipe(player, recipe.professionId, recipe.spellId)))
        return false;
    return true;
}

RecipeData const* CraftingOrders::ResolveRecipeItem(ItemPrototype const* proto, uint32 professionId, uint32& taughtSpell) const
{
    taughtSpell = 0;
    if (!proto)
        return nullptr;

    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (!proto->Spells[i].SpellId)
            continue;
        uint32 candidate = ResolveCraftSpell(proto->Spells[i].SpellId);
        if (!candidate)
            continue;
        RecipeData const* recipe = GetRecipeForSpell(candidate);
        if (recipe && recipe->professionId == professionId)
        {
            taughtSpell = candidate;
            return recipe;
        }
    }
    return nullptr;
}

void CraftingOrders::ForEachInventoryItem(Player* player, std::function<void(Item*)> const& fn) const
{
    if (!player)
        return;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            fn(item);

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Item* bagItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        Bag* bagPtr = bagItem ? bagItem->ToBag() : nullptr;
        if (!bagPtr)
            continue;
        for (uint32 slot = 0; slot < bagPtr->GetBagSize(); ++slot)
            if (Item* item = player->GetItemByPos(bag, uint8(slot)))
                fn(item);
    }
}

Item* CraftingOrders::FindOwnedItem(Player* player, uint32 bag, uint32 slot, uint32 expectedEntry) const
{
    if (!player)
        return nullptr;
    Item* item = player->GetItemByPos(uint8(bag), uint8(slot));
    if (!item)
        return nullptr;
    if (expectedEntry && item->GetEntry() != expectedEntry)
        return nullptr;
    if (item->GetOwnerGuid() != player->GetObjectGuid())
        return nullptr;
    return item;
}

bool CraftingOrders::OpenSession(Player* player, Creature* creature, uint32 serviceOverride)
{
    if (!player || !creature || !Enabled())
        return false;
    NpcBinding binding;
    if (!ResolveNpcBinding(creature, binding))
        return false;
    uint32 service = serviceOverride ? serviceOverride : binding.service;
    if (service == CraftingOrdersDomain::SERVICE_DISENCHANT && !sCraftingOrdersConfig.DisenchantEnabled())
        return false;
    if (service == CraftingOrdersDomain::SERVICE_DISENCHANT &&
        binding.professionId == CraftingOrdersDomain::PROF_ENCHANTING &&
        !sCraftingOrdersConfig.EnchantingDisenchantEnabled())
        return false;

    CraftingSession session;
    session.playerGuid = player->GetGUIDLow();
    session.creatureGuid = creature->GetGUIDLow();
    session.creatureEntry = creature->GetEntry();
    session.mapId = player->GetMapId();
    session.x = player->GetPositionX();
    session.y = player->GetPositionY();
    session.z = player->GetPositionZ();
    session.professionId = binding.professionId;
    session.service = service;
    session.maxSkillRank = binding.maxSkillRank;
    session.createdMs = GetNowMs();
    session.expiresMs = session.createdMs + CraftingOrdersDomain::SESSION_TTL_MS;
    _sessions[session.playerGuid] = session;
    return true;
}

CraftingSession* CraftingOrders::GetSession(Player* player)
{
    if (!player)
        return nullptr;
    auto it = _sessions.find(player->GetGUIDLow());
    return it != _sessions.end() ? &it->second : nullptr;
}

void CraftingOrders::CloseSession(uint32 playerGuid)
{
    _sessions.erase(playerGuid);
}

void CraftingOrders::CloseSession(Player* player)
{
    if (player)
        CloseSession(player->GetGUIDLow());
}

Creature* CraftingOrders::ResolveSessionCreature(Player* player, CraftingSession const& session) const
{
    if (!player || !player->GetMap())
        return nullptr;
    Creature* creature = player->GetMap()->GetCreature(ObjectGuid(HIGHGUID_UNIT, session.creatureEntry, session.creatureGuid));
    if (!creature || !creature->IsAlive())
        return nullptr;
    if (!player->IsWithinDistInMap(creature, INTERACTION_DISTANCE))
        return nullptr;
    return creature;
}

bool CraftingOrders::ValidateSession(Player* player, uint32 expectedService, std::string& error)
{
    CraftingSession* session = GetSession(player);
    if (!session)
    {
        error = "no active NPC session";
        return false;
    }
    if (DeadlineReached(GetNowMs(), session->expiresMs))
    {
        CloseSession(player);
        error = "session expired";
        return false;
    }
    if (player->GetMapId() != session->mapId)
    {
        CloseSession(player);
        error = "wrong map";
        return false;
    }
    if (!ResolveSessionCreature(player, *session))
    {
        CloseSession(player);
        error = "NPC is too far away";
        return false;
    }
    if (expectedService && session->service != expectedService)
    {
        error = "this NPC does not provide that service";
        return false;
    }
    session->expiresMs = GetNowMs() + CraftingOrdersDomain::SESSION_TTL_MS;
    return true;
}

bool CraftingOrders::RateLimit(CraftingSession& session, uint32 nowMs)
{
    if (nowMs - session.windowStartMs >= CraftingOrdersDomain::RATE_LIMIT_WINDOW_MS)
    {
        session.windowStartMs = nowMs;
        session.windowCount = 0;
    }
    ++session.windowCount;
    return session.windowCount <= CraftingOrdersDomain::RATE_LIMIT_MAX;
}

std::vector<uint32> const& CraftingOrders::GetEnchantEquipmentSlots()
{
    static std::vector<uint32> const slots = {
        EQUIPMENT_SLOT_HEAD,
        EQUIPMENT_SLOT_SHOULDERS,
        EQUIPMENT_SLOT_CHEST,
        EQUIPMENT_SLOT_WRISTS,
        EQUIPMENT_SLOT_HANDS,
        EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS,
        EQUIPMENT_SLOT_FEET,
        EQUIPMENT_SLOT_BACK,
        EQUIPMENT_SLOT_MAINHAND,
        EQUIPMENT_SLOT_OFFHAND,
        EQUIPMENT_SLOT_RANGED,
        EQUIPMENT_SLOT_FINGER1,
        EQUIPMENT_SLOT_FINGER2
    };
    return slots;
}

std::string CraftingOrders::GetSlotName(uint32 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD: return "Head";
        case EQUIPMENT_SLOT_SHOULDERS: return "Shoulder";
        case EQUIPMENT_SLOT_CHEST: return "Chest";
        case EQUIPMENT_SLOT_WRISTS: return "Wrist";
        case EQUIPMENT_SLOT_HANDS: return "Hands";
        case EQUIPMENT_SLOT_WAIST: return "Waist";
        case EQUIPMENT_SLOT_LEGS: return "Legs";
        case EQUIPMENT_SLOT_FEET: return "Feet";
        case EQUIPMENT_SLOT_BACK: return "Back";
        case EQUIPMENT_SLOT_MAINHAND: return "Main Hand";
        case EQUIPMENT_SLOT_OFFHAND: return "Off Hand";
        case EQUIPMENT_SLOT_RANGED: return "Ranged";
        case EQUIPMENT_SLOT_FINGER1: return "Finger 1";
        case EQUIPMENT_SLOT_FINGER2: return "Finger 2";
        default: return "Unknown";
    }
}

bool CraftingOrders::Craft(Player* player, uint32 spellId, uint32 quantity, std::string& result)
{
    std::string error;
    if (!ValidateSession(player, 0, error))
    {
        result = error;
        return false;
    }
    CraftingSession* session = GetSession(player);
    if (!session || (session->service != CraftingOrdersDomain::SERVICE_CRAFT &&
                     session->service != CraftingOrdersDomain::SERVICE_ENCHANT))
    {
        result = "this NPC does not provide crafting";
        return false;
    }
    RecipeData const* recipe = GetRecipeForSpell(spellId);
    if (!recipe || recipe->professionId != session->professionId)
    {
        result = "recipe not available";
        return false;
    }
    if (!CraftingOrdersDomain::RecipeWithinSkillCap(recipe->reqSkillRank, session->maxSkillRank))
    {
        result = "this recipe is above the trainer's skill level";
        return false;
    }
    if (!PlayerCanUseRecipe(player, *recipe))
    {
        result = "recipe not available";
        return false;
    }
    if (recipe->isEnchantSpell && !recipe->createdItemId)
    {
        result = "use the enchant action for that formula";
        return false;
    }

    uint32 qty = 0;
    if (!CraftingOrdersDomain::ClampQuantity(quantity, sCraftingOrdersConfig.MaxQuantity(), qty))
    {
        result = "invalid quantity";
        return false;
    }

    uint32 outputCount = 0;
    if (!CraftingOrdersDomain::TotalOutputCount(recipe->createdItemCount, qty, outputCount))
    {
        result = "output overflow";
        return false;
    }
    uint32 feeEach = CalculateGoldFee(*recipe);
    uint32 feeTotal = 0;
    if (!CraftingOrdersDomain::TotalFee(feeEach, qty, feeTotal))
    {
        result = "fee overflow";
        return false;
    }
    if (!ValidateMaterials(*recipe, player, qty, error))
    {
        result = error;
        return false;
    }
    if (player->GetMoney() < feeTotal)
    {
        result = "You need " + CraftingOrdersDomain::FormatMoney(feeTotal) + " for the crafting fee";
        return false;
    }
    if (sCraftingOrdersConfig.EnforceCooldowns() && IsOnCooldown(player, spellId))
    {
        result = "This recipe is on cooldown";
        return false;
    }

    uint32 craftingTimeMinutes = sCraftingOrdersConfig.CraftingTimeMinutes();
    bool deliverByMail = craftingTimeMinutes > 0;

    ItemPosCountVec dest;
    if (!deliverByMail && player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe->createdItemId, outputCount) != EQUIP_ERR_OK)
    {
        result = "Not enough inventory space";
        return false;
    }

    std::vector<Item*> mailedItems;
    auto cleanupMailedItems = [&mailedItems]()
    {
        for (Item* item : mailedItems)
            delete item;
        mailedItems.clear();
    };

    if (deliverByMail)
    {
        ItemPrototype const* outputProto = sObjectMgr.GetItemPrototype(recipe->createdItemId);
        uint32 maxStackSize = outputProto ? outputProto->GetMaxStackSize() : 1;
        if (!maxStackSize)
            maxStackSize = 1;

        uint32 remaining = outputCount;
        while (remaining)
        {
            uint32 stackCount = std::min(remaining, maxStackSize);
            Item* item = Item::CreateItem(recipe->createdItemId, stackCount, player);
            if (!item)
            {
                cleanupMailedItems();
                sLog.outError("[mod-crafting-orders] Failed to create delayed crafted item %u for player %u.",
                    recipe->createdItemId, player->GetGUIDLow());
                result = "Failed to create crafted item; no materials or fee were charged";
                return false;
            }

            mailedItems.push_back(item);
            remaining -= stackCount;
        }
    }

    // Snapshot reagent stacks before storing the result.  This matters for
    // the (unusual but valid) recipe where the crafted item is also one of
    // its reagents: StoreNewItem may merge the output into an existing stack,
    // and a subsequent DestroyItemCount(itemId, ...) would otherwise destroy
    // part of the newly crafted output as well.
    struct MaterialSource
    {
        uint32 itemGuidLow;
        uint32 count;
    };
    struct MaterialPlan
    {
        uint32 itemId;
        uint32 count;
        std::vector<MaterialSource> sources;
    };
    std::vector<MaterialPlan> materialPlans;
    for (CraftMaterial const& mat : recipe->materials)
    {
        uint32 needed = 0;
        CraftingOrdersDomain::TotalReagentCount(mat.count, qty, needed);

        MaterialPlan* plan = nullptr;
        for (MaterialPlan& candidate : materialPlans)
        {
            if (candidate.itemId == mat.itemId)
            {
                plan = &candidate;
                break;
            }
        }
        if (!plan)
        {
            materialPlans.push_back({ mat.itemId, needed, {} });
            plan = &materialPlans.back();
        }
        else if (!CraftingOrdersDomain::CheckedAddU32(plan->count, needed, plan->count))
        {
            result = "reagent total overflow";
            return false;
        }

    }
    for (MaterialPlan& plan : materialPlans)
    {
        uint64 available = 0;
        player->ApplyForAllItems([&](Item* item)
        {
            if (item && item->GetEntry() == plan.itemId && IsConsumableReagent(item))
            {
                plan.sources.push_back({ item->GetGUIDLow(), item->GetCount() });
                available += item->GetCount();
            }
        });
        if (available < plan.count)
        {
            result = "Crafting inventory changed; please try again";
            return false;
        }
    }

    // Store or prepare the result before charging the player. CanStoreNewItem
    // is only a preflight check; item creation/storage can still fail (for
    // example if an item hook rejects it). Never consume reagents or gold
    // until the result is known to be ready for delivery.
    Item* created = nullptr;
    if (!deliverByMail)
    {
        created = player->StoreNewItem(dest, recipe->createdItemId, true, 0);
        if (!created)
        {
            sLog.outError("[mod-crafting-orders] Failed to store crafted item %u for player %u after a successful inventory preflight.",
                recipe->createdItemId, player->GetGUIDLow());
            result = "Failed to create crafted item; no materials or fee were charged";
            return false;
        }
    }

    for (MaterialPlan const& plan : materialPlans)
    {
        uint32 remaining = plan.count;
        for (MaterialSource const& source : plan.sources)
        {
            if (!remaining)
                break;
            Item* sourceItem = player->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, source.itemGuidLow));
            if (!sourceItem || sourceItem->GetEntry() != plan.itemId || !IsConsumableReagent(sourceItem))
            {
                cleanupMailedItems();
                sLog.outError("[mod-crafting-orders] Reagent %u changed during craft commit for player %u after preparing crafted item.",
                    plan.itemId, player->GetGUIDLow());
                result = "Crafting inventory changed unexpectedly; please contact a game master";
                return false;
            }
            uint32 requested = std::min(remaining, source.count);
            uint32 unremoved = requested;
            // Use the item-specific overload so an output merged into a
            // reagent stack is not included in the amount consumed.
            player->DestroyItemCount(sourceItem, unremoved, true);
            remaining -= requested - unremoved;
            if (unremoved)
            {
                cleanupMailedItems();
                sLog.outError("[mod-crafting-orders] Failed to consume reagent %u for player %u after preparing crafted item.",
                    plan.itemId, player->GetGUIDLow());
                result = "Failed to consume all crafting materials; please contact a game master";
                return false;
            }
        }
        if (remaining)
        {
            cleanupMailedItems();
            sLog.outError("[mod-crafting-orders] Reagent plan for item %u was incomplete for player %u after preparing crafted item.",
                plan.itemId, player->GetGUIDLow());
            result = "Failed to consume all crafting materials; please contact a game master";
            return false;
        }
    }
    player->ModifyMoney(-int32(feeTotal));

    if (deliverByMail)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(recipe->createdItemId);
        std::string itemName = proto ? proto->Name1 : "item";
        for (Item* item : mailedItems)
        {
            // This core exposes one mail attachment to the Vanilla client, so
            // split multi-stack results into separate delayed mails.
            item->SaveToDB();
            MailDraft mail("Crafting Order Complete", "Your crafted item is ready.");
            mail.AddItem(item);
            mail.SendMailTo(MailReceiver(player, player->GetObjectGuid()),
                MailSender(MAIL_CREATURE, session->creatureEntry), MAIL_CHECK_MASK_COPIED,
                craftingTimeMinutes * MINUTE);
        }

        result = "Crafted: " + itemName + " x" + std::to_string(outputCount) +
            "; arriving by mail in " + std::to_string(craftingTimeMinutes) +
            (craftingTimeMinutes == 1 ? " minute" : " minutes");
    }
    else
    {
        player->SendNewItem(created, outputCount, true, false);

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(recipe->createdItemId);
        result = std::string("Crafted: ") + (proto ? proto->Name1 : "item") + " x" + std::to_string(outputCount);
    }

    if (sCraftingOrdersConfig.EnforceCooldowns() && recipe->spellCooldownSecs > 0)
        SetCooldown(player, spellId, recipe->spellCooldownSecs);

    return true;
}

bool CraftingOrders::Enchant(Player* player, uint32 spellId, uint32 bag, uint32 slot, std::string& result)
{
    std::string error;
    if (!ValidateSession(player, CraftingOrdersDomain::SERVICE_ENCHANT, error))
    {
        result = error;
        return false;
    }

    RecipeData const* recipe = GetRecipeForSpell(spellId);
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    if (!recipe || !recipe->isEnchantSpell || !spellInfo)
    {
        result = "enchant not available";
        return false;
    }
    if (recipe->professionId != CraftingOrdersDomain::PROF_ENCHANTING)
    {
        result = "enchant not available";
        return false;
    }
    CraftingSession* session = GetSession(player);
    if (!session || recipe->professionId != session->professionId ||
        !CraftingOrdersDomain::RecipeWithinSkillCap(recipe->reqSkillRank, session->maxSkillRank))
    {
        result = "this enchant is above the trainer's skill level";
        return false;
    }
    if (!PlayerCanUseRecipe(player, *recipe))
    {
        result = "enchant not available";
        return false;
    }
    if (!recipe->isPermanentEnchant)
    {
        result = "Temporary enchants are not supported";
        return false;
    }
    if (!recipe->enchantId)
    {
        result = "Could not determine enchantment ID";
        return false;
    }

    // SetEnchantment is a void mutator and silently accepts an ID that is not
    // present in SpellItemEnchantment.dbc.  Validate it before charging the
    // player so malformed/stale recipe data cannot consume reagents and gold
    // without applying a usable enchantment.
    if (!sSpellItemEnchantmentStore.LookupEntry(recipe->enchantId))
    {
        result = "Could not resolve enchantment data";
        return false;
    }

    Item* item = FindOwnedItem(player, bag, slot);
    if (!item || item->IsInTrade() || !item->IsFitToSpellRequirements(spellInfo))
    {
        result = "The selected item is no longer valid for that enchant";
        return false;
    }
    if (!ValidateMaterials(*recipe, player, 1, error))
    {
        result = error;
        return false;
    }
    uint32 fee = CalculateGoldFee(*recipe);
    if (player->GetMoney() < fee)
    {
        result = "You need " + CraftingOrdersDomain::FormatMoney(fee) + " for the crafting fee";
        return false;
    }
    if (sCraftingOrdersConfig.EnforceCooldowns() && IsOnCooldown(player, spellId))
    {
        result = "This enchant is on cooldown";
        return false;
    }

    for (CraftMaterial const& mat : recipe->materials)
        player->DestroyItemCount(mat.itemId, mat.count, true, false);
    player->ModifyMoney(-int32(fee));

    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
    item->SetEnchantment(PERM_ENCHANTMENT_SLOT, recipe->enchantId, 0, 0);
    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
    item->SetState(ITEM_CHANGED, player);

    if (sCraftingOrdersConfig.EnforceCooldowns() && recipe->spellCooldownSecs > 0)
        SetCooldown(player, spellId, recipe->spellCooldownSecs);

    result = std::string("Applied enchant to ") + item->GetProto()->Name1;
    return true;
}

bool CraftingOrders::Disenchant(Player* player, uint32 bag, uint32 slot, std::string& result)
{
    std::string error;
    if (!ValidateSession(player, CraftingOrdersDomain::SERVICE_DISENCHANT, error))
    {
        result = error;
        return false;
    }
    if (!sCraftingOrdersConfig.DisenchantEnabled())
    {
        result = "Disenchanting is disabled";
        return false;
    }

    Item* item = FindOwnedItem(player, bag, slot);
    if (!item || item->IsInTrade())
    {
        result = "The selected item is no longer in your inventory";
        return false;
    }
    ItemPrototype const* proto = item->GetProto();
    if (!proto || proto->DisenchantID == 0 || (proto->Flags & ITEM_FLAG_NO_DISENCHANT) ||
        proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_QUEST_ITEM1 || proto->IsQuestItem ||
        (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) ||
        !LootTemplates_Disenchant.HaveLootFor(proto->DisenchantID))
    {
        result = "This item cannot be disenchanted";
        return false;
    }

    uint32 fee = CraftingOrdersDomain::FeeForItemLevel(
        sCraftingOrdersConfig.Fees().disenchantFees, proto->ItemLevel, sCraftingOrdersConfig.Fees().disenchantDefaultFee);
    if (player->GetMoney() < fee)
    {
        result = "You do not have enough gold for the disenchanting fee";
        return false;
    }

    Loot loot(player);
    if (!loot.FillLoot(proto->DisenchantID, LootTemplates_Disenchant, player, true, true))
    {
        result = "Disenchant produced no result";
        return false;
    }

    std::vector<std::pair<uint32, uint32>> awards;
    auto collect = [&](LootItem const& lootItem)
    {
        if (lootItem.is_looted || !sObjectMgr.GetItemPrototype(lootItem.itemid) || lootItem.count == 0)
            return;
        awards.push_back({ lootItem.itemid, lootItem.count });
    };
    for (LootItem const& lootItem : loot.items)
        collect(lootItem);
    for (LootItem const& lootItem : loot.m_questItems)
        collect(lootItem);

    if (awards.empty())
    {
        result = "Disenchant produced no result";
        return false;
    }

    std::vector<Item*> created;
    auto cleanupCreated = [&created]()
    {
        for (Item* awardItem : created)
            delete awardItem;
        created.clear();
    };

    for (auto const& award : awards)
    {
        ItemPrototype const* awardProto = sObjectMgr.GetItemPrototype(award.first);
        if (!awardProto || award.second == 0)
        {
            cleanupCreated();
            result = "Disenchant produced no result";
            return false;
        }
        uint32 remaining = award.second;
        uint32 stackSize = awardProto->GetMaxStackSize();
        if (stackSize == 0)
            stackSize = 1;
        while (remaining > 0)
        {
            uint32 n = remaining < stackSize ? remaining : stackSize;
            Item* awardItem = Item::CreateItem(award.first, n, player);
            if (!awardItem)
            {
                cleanupCreated();
                result = "Failed to create disenchant result";
                return false;
            }
            created.push_back(awardItem);
            remaining -= n;
        }
    }

    if (created.empty())
    {
        result = "Disenchant produced no result";
        return false;
    }

    if (player->CanStoreItems(created.data(), int(created.size())) != EQUIP_ERR_OK)
    {
        cleanupCreated();
        result = "Not enough inventory space for disenchant results";
        return false;
    }

    std::string itemName = proto->Name1;
    uint8 itemBag = item->GetBagSlot();
    uint8 itemSlot = item->GetSlot();
    uint32 npcEntry = 0;
    if (CraftingSession* session = GetSession(player))
        npcEntry = session->creatureEntry;

    player->ModifyMoney(-int32(fee));
    player->DestroyItem(itemBag, itemSlot, true);

    bool mailed = false;
    for (size_t i = 0; i < created.size(); ++i)
    {
        Item* awardItem = created[i];
        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, awardItem, false) == EQUIP_ERR_OK)
        {
            if (Item* stored = player->StoreItem(dest, awardItem, true))
            {
                player->SendNewItem(stored, stored->GetCount(), true, false);
                created[i] = nullptr;
                continue;
            }
        }

        std::vector<Item*> leftover;
        for (size_t j = i; j < created.size(); ++j)
        {
            if (created[j])
                leftover.push_back(created[j]);
            created[j] = nullptr;
        }
        sLog.outError("[mod-crafting-orders] Disenchant post-consume store failed for player %u; mailing %u leftover stacks.",
            player->GetGUIDLow(), uint32(leftover.size()));
        MailDisenchantRecovery(player, leftover, npcEntry);
        mailed = true;
        break;
    }

    if (mailed)
        result = "Successfully disenchanted [" + itemName + "]; leftover materials were mailed";
    else
        result = "Successfully disenchanted [" + itemName + "] for " + CraftingOrdersDomain::FormatMoney(fee);
    return true;
}

void CraftingOrders::MailDisenchantRecovery(Player* player, std::vector<Item*>& items, uint32 npcEntry) const
{
    if (!player)
    {
        for (Item* awardItem : items)
            delete awardItem;
        items.clear();
        return;
    }

    uint32 senderEntry = npcEntry ? npcEntry : 1;
    for (Item* awardItem : items)
    {
        if (!awardItem)
            continue;
        awardItem->SaveToDB();
        MailDraft("Crafting Orders: Disenchant recovery",
            "Your bags were full after disenchanting. The materials are attached.")
            .AddItem(awardItem)
            .SendMailTo(MailReceiver(player, player->GetObjectGuid()),
                MailSender(MAIL_CREATURE, senderEntry),
                MAIL_CHECK_MASK_HAS_BODY);
    }
    items.clear();
}

bool CraftingOrders::HandIn(Player* player, uint32 itemGuidLow, std::string& result)
{
    std::string error;
    if (!ValidateSession(player, 0, error))
    {
        result = error;
        return false;
    }
    CraftingSession* session = GetSession(player);
    if (session->service == CraftingOrdersDomain::SERVICE_DISENCHANT)
    {
        result = "this NPC does not accept recipe hand-ins";
        return false;
    }

    Item* item = player->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, itemGuidLow));
    if (!item || item->GetOwnerGuid() != player->GetObjectGuid() || item->IsInTrade())
    {
        result = "recipe item not found";
        return false;
    }

    uint32 taughtSpell = 0;
    RecipeData const* recipe = ResolveRecipeItem(item->GetProto(), session->professionId, taughtSpell);
    if (!recipe || !taughtSpell)
    {
        result = "that item does not teach a recipe for this profession";
        return false;
    }
    if (!recipe->requiresUnlock)
    {
        result = "that recipe is already available from trainers";
        return false;
    }
    if (HasPlayerRecipe(player, session->professionId, taughtSpell))
    {
        result = "you already unlocked that recipe";
        return false;
    }

    std::string recipeName = item->GetProto()->Name1;
    if (!AddPlayerRecipe(player, session->professionId, taughtSpell))
    {
        result = "failed to save recipe unlock";
        return false;
    }
    // Recipe items can be stackable. Consume exactly one copy from the
    // selected item, rather than the first matching stack in the inventory.
    uint32 consumeCount = 1;
    player->DestroyItemCount(item, consumeCount, true);
    result = "Unlocked recipe: " + recipeName;
    return true;
}

bool CraftingOrders::HandInRecipe(Player* player, uint32 itemGuidLow, std::string& result)
{
    return HandIn(player, itemGuidLow, result);
}
