#include "CraftingOrders.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"
#include <memory>

namespace
{
    constexpr uint32 NPC_ENTRY_MIN = 5110001;
    constexpr uint32 NPC_ENTRY_MAX = 5110024;

    bool TableExists(DatabaseType& db, char const* table)
    {
        std::unique_ptr<QueryResult> result(db.PQuery("SHOW TABLES LIKE '%s'", table));
        return bool(result);
    }
}

void CraftingOrders::LoadNpcBindings()
{
    _npcBindings.clear();
    if (!TableExists(WorldDatabase, "crafting_order_npc"))
    {
        // Dedicated NPC bindings are legacy compatibility data. Runtime
        // profession-trainer discovery does not depend on this table, so a
        // fresh installation without the optional compatibility migration
        // must still be able to serve existing trainers.
        sLog.outString("[mod-crafting-orders] Optional table crafting_order_npc is missing; using runtime trainer discovery.");
        return;
    }

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT entry, professionId, service FROM crafting_order_npc"));
    if (!result)
    {
        sLog.outString("[mod-crafting-orders] No legacy crafting_order_npc bindings found; using runtime trainer discovery.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        NpcBinding binding;
        binding.entry = fields[0].GetUInt32();
        binding.professionId = fields[1].GetUInt32();
        binding.service = fields[2].GetUInt32();
        binding.available = true;

        if (binding.service == CraftingOrdersDomain::SERVICE_CRAFT ||
            binding.service == CraftingOrdersDomain::SERVICE_ENCHANT)
        {
            if (!CraftingOrdersDomain::IsSupportedProfession(binding.professionId))
            {
                sLog.outError("[mod-crafting-orders] NPC %u has unsupported profession %u; ignored.",
                    binding.entry, binding.professionId);
                continue;
            }
        }
        else if (binding.service != CraftingOrdersDomain::SERVICE_DISENCHANT)
        {
            sLog.outError("[mod-crafting-orders] NPC %u has unknown service %u; ignored.",
                binding.entry, binding.service);
            continue;
        }

        std::unique_ptr<QueryResult> existing(WorldDatabase.PQuery(
            "SELECT name, script_name FROM creature_template WHERE entry = %u", binding.entry));
        if (!existing)
        {
            sLog.outError("[mod-crafting-orders] Bound creature %u is missing from creature_template.", binding.entry);
            binding.available = false;
        }
        else
        {
            Field* cfields = existing->Fetch();
            std::string scriptName = cfields[1].GetCppString();
            bool ours = scriptName == "crafting_order" ||
                        scriptName == "crafting_order_enchant" ||
                        scriptName == "crafting_order_disenchant";
            if (!ours)
            {
                if (binding.entry >= NPC_ENTRY_MIN && binding.entry <= NPC_ENTRY_MAX)
                {
                    sLog.outError("[mod-crafting-orders] Creature entry %u is occupied by '%s' / script '%s'; refusing to bind.",
                        binding.entry, cfields[0].GetCppString().c_str(), scriptName.c_str());
                }
                else
                {
                    sLog.outError("[mod-crafting-orders] Creature entry %u uses script '%s'; set a crafting-orders script before binding it.",
                        binding.entry, scriptName.c_str());
                }
                binding.available = false;
            }
            binding.scriptName = scriptName;
        }

        _npcBindings[binding.entry] = binding;
    } while (result->NextRow());

    sLog.outString("[mod-crafting-orders] Loaded %u NPC bindings.", uint32(_npcBindings.size()));
}

bool CraftingOrders::HasPlayerRecipe(Player* player, uint32 professionId, uint32 spellId) const
{
    if (!player || !player->GetSession())
        return false;
    if (!TableExists(CharacterDatabase, "crafting_order_recipes"))
        return false;

    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT 1 FROM crafting_order_recipes WHERE accountId = %u AND professionId = %u AND spellId = %u",
        player->GetSession()->GetAccountId(), professionId, spellId));
    return bool(result);
}

bool CraftingOrders::AddPlayerRecipe(Player* player, uint32 professionId, uint32 spellId)
{
    if (!player || !player->GetSession())
        return false;
    if (!TableExists(CharacterDatabase, "crafting_order_recipes"))
        return false;

    if (!CharacterDatabase.DirectPExecute(
        "INSERT IGNORE INTO crafting_order_recipes (accountId, professionId, spellId) VALUES (%u, %u, %u)",
        player->GetSession()->GetAccountId(), professionId, spellId))
        return false;
    return HasPlayerRecipe(player, professionId, spellId);
}

bool CraftingOrders::IsOnCooldown(Player* player, uint32 spellId) const
{
    if (!player || !player->GetSession() || !sCraftingOrdersConfig.EnforceCooldowns())
        return false;
    if (!TableExists(CharacterDatabase, "crafting_order_cooldowns"))
        return false;

    uint64 const now = uint64(time(nullptr));
    std::unique_ptr<QueryResult> result;
    if (sCraftingOrdersConfig.AccountWideCooldowns())
    {
        result.reset(CharacterDatabase.PQuery(
            "SELECT cooldownEndTime FROM crafting_order_cooldowns "
            "WHERE scopeType = %u AND scopeId = %u AND spellId = %u AND cooldownEndTime > " UI64FMTD,
            uint32(CraftingOrdersDomain::COOLDOWN_SCOPE_ACCOUNT),
            player->GetSession()->GetAccountId(), spellId, now));
    }
    else
    {
        result.reset(CharacterDatabase.PQuery(
            "SELECT cooldownEndTime FROM crafting_order_cooldowns "
            "WHERE scopeType = %u AND scopeId = %u AND spellId = %u AND cooldownEndTime > " UI64FMTD,
            uint32(CraftingOrdersDomain::COOLDOWN_SCOPE_CHARACTER),
            player->GetGUIDLow(), spellId, now));
    }
    return bool(result);
}

void CraftingOrders::SetCooldown(Player* player, uint32 spellId, uint32 cooldownSecs)
{
    if (!player || !player->GetSession() || !cooldownSecs)
        return;

    uint64 const endTime = uint64(time(nullptr)) + uint64(cooldownSecs);
    uint32 scopeType = sCraftingOrdersConfig.AccountWideCooldowns()
        ? uint32(CraftingOrdersDomain::COOLDOWN_SCOPE_ACCOUNT)
        : uint32(CraftingOrdersDomain::COOLDOWN_SCOPE_CHARACTER);
    uint32 scopeId = sCraftingOrdersConfig.AccountWideCooldowns()
        ? player->GetSession()->GetAccountId()
        : player->GetGUIDLow();

    CharacterDatabase.DirectPExecute(
        "REPLACE INTO crafting_order_cooldowns (scopeType, scopeId, spellId, cooldownEndTime) "
        "VALUES (%u, %u, %u, " UI64FMTD ")",
        scopeType, scopeId, spellId, endTime);
}

void CraftingOrders::CleanupExpiredCooldowns()
{
    if (!TableExists(CharacterDatabase, "crafting_order_cooldowns"))
        return;

    uint64 const now = uint64(time(nullptr));
    uint64 const maxEnd = now + (30ull * 24ull * 60ull * 60ull);
    CharacterDatabase.PExecute(
        "DELETE FROM crafting_order_cooldowns WHERE cooldownEndTime <= " UI64FMTD " OR cooldownEndTime > " UI64FMTD,
        now, maxEnd);
}
