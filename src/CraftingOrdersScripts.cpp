#include "CraftingOrders.h"
#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptObjects.h"
#include "ScriptedGossip.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    enum GossipAction : uint32
    {
        ACTION_BROWSE = GOSSIP_ACTION_INFO_DEF + 1,
        ACTION_HANDIN = GOSSIP_ACTION_INFO_DEF + 2,
        ACTION_DISENCHANT = GOSSIP_ACTION_INFO_DEF + 3,
        ACTION_BACK_MAIN = GOSSIP_ACTION_INFO_DEF + 100,
        ACTION_HANDIN_BASE = GOSSIP_ACTION_INFO_DEF + 500
    };

    constexpr uint32 GOSSIP_TEXT_ID = 1;
    // Keep module actions separate from the conventional MAIN sender. This
    // prevents a coincidental action value in an unrelated trainer script
    // from being consumed by the packet hook.
    constexpr uint32 CRAFTING_GOSSIP_SENDER = 0x4352;

    struct HandInGossipState
    {
        uint32 creatureGuid = 0;
        uint32 professionId = 0;
        std::vector<CraftingOrdersDomain::HandInRecord> entries;
    };

    std::unordered_map<uint32, HandInGossipState> HandInGossipStates;

    struct NativeTrainerMenuState
    {
        uint32 creatureGuid = 0;
        std::vector<GossipMenuItem> gossipItems;
        std::vector<QuestMenuItem> questItems;
    };

    std::unordered_map<uint32, NativeTrainerMenuState> NativeTrainerMenus;

    bool IsHandInRecipeAction(uint32 action)
    {
        return action >= ACTION_HANDIN_BASE && action < ACTION_HANDIN_BASE + 100;
    }

    bool IsCraftingGossipAction(uint32 action)
    {
        return action == ACTION_BROWSE || action == ACTION_HANDIN ||
            action == ACTION_DISENCHANT || action == ACTION_BACK_MAIN ||
            IsHandInRecipeAction(action);
    }

    bool HasGossipMenuItem(GossipMenu& menu, GossipMenuItem const& wanted)
    {
        for (unsigned int item = 0; item < menu.MenuItemCount(); ++item)
        {
            GossipMenuItem const& existing = menu.GetItem(item);
            if (existing.m_gIcon == wanted.m_gIcon &&
                existing.m_gCoded == wanted.m_gCoded &&
                existing.m_gMessage == wanted.m_gMessage &&
                existing.m_gSender == wanted.m_gSender &&
                existing.m_gOptionId == wanted.m_gOptionId &&
                existing.m_gBoxMessage == wanted.m_gBoxMessage)
                return true;
        }
        return false;
    }

    void CacheNativeTrainerMenu(Player* player, Creature* creature)
    {
        if (!player || !creature || !player->PlayerTalkClass)
            return;

        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        NativeTrainerMenuState state;
        state.creatureGuid = creature->GetGUIDLow();
        for (unsigned int item = 0; item < menu.MenuItemCount(); ++item)
        {
            GossipMenuItem const& gossipItem = menu.GetItem(item);
            if (gossipItem.m_gSender != CRAFTING_GOSSIP_SENDER)
                state.gossipItems.push_back(gossipItem);
        }
        for (uint8 item = 0; item < player->PlayerTalkClass->GetQuestMenu().MenuItemCount(); ++item)
            state.questItems.push_back(player->PlayerTalkClass->GetQuestMenu().GetItem(item));

        NativeTrainerMenus[player->GetGUIDLow()] = state;
    }

    void RestoreNativeTrainerMenu(Player* player, Creature* creature)
    {
        if (!player || !creature || !player->PlayerTalkClass)
            return;

        player->PrepareGossipMenu(creature, creature->GetDefaultGossipMenuId());

        auto it = NativeTrainerMenus.find(player->GetGUIDLow());
        if (it == NativeTrainerMenus.end() || it->second.creatureGuid != creature->GetGUIDLow())
            return;

        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        for (GossipMenuItem const& gossipItem : it->second.gossipItems)
        {
            if (HasGossipMenuItem(menu, gossipItem) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
                continue;
            menu.AddMenuItem(gossipItem.m_gIcon, gossipItem.m_gMessage, gossipItem.m_gSender,
                gossipItem.m_gOptionId, gossipItem.m_gBoxMessage, gossipItem.m_gCoded);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        for (QuestMenuItem const& questItem : it->second.questItems)
            if (!player->PlayerTalkClass->GetQuestMenu().HasItem(questItem.m_qId))
                player->PlayerTalkClass->GetQuestMenu().AddMenuItem(questItem.m_qId, questItem.m_qIcon);
    }

    void SendInit(Player* player, NpcBinding const* binding, uint32 uiMode, uint32 serviceOverride = 0)
    {
        uint32 service = serviceOverride ? serviceOverride : binding->service;
        std::string payload = std::to_string(binding->professionId) + "\t" +
            CraftingOrdersDomain::ProfessionName(binding->professionId) + "\t" +
            std::to_string(service) + "\t" +
            std::to_string(uiMode);
        sCraftingOrders.SendAddon(player, 0, "INIT", payload);
    }

    bool SendMainMenu(Player* player, Creature* creature, bool preserveDefaultMenu = false,
        bool openSession = true)
    {
        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return false;
        if (openSession && !sCraftingOrders.OpenSession(player, creature))
            return false;

        HandInGossipStates.erase(player->GetGUIDLow());

        if (preserveDefaultMenu)
            RestoreNativeTrainerMenu(player, creature);
        else
            player->PlayerTalkClass->ClearMenus();

        if (binding.service == CraftingOrdersDomain::SERVICE_DISENCHANT)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Disenchant an item", CRAFTING_GOSSIP_SENDER, ACTION_BROWSE);
        else if (binding.service == CraftingOrdersDomain::SERVICE_ENCHANT)
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, "Browse enchants", CRAFTING_GOSSIP_SENDER, ACTION_BROWSE);
            if (sCraftingOrdersConfig.EnchantingDisenchantEnabled())
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Disenchant an item", CRAFTING_GOSSIP_SENDER, ACTION_DISENCHANT);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Hand in a formula", CRAFTING_GOSSIP_SENDER, ACTION_HANDIN);
        }
        else
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, "Browse recipes", CRAFTING_GOSSIP_SENDER, ACTION_BROWSE);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Hand in a recipe", CRAFTING_GOSSIP_SENDER, ACTION_HANDIN);
        }
        if (preserveDefaultMenu)
            player->SendPreparedGossip(creature);
        else
            player->SEND_GOSSIP_MENU(GOSSIP_TEXT_ID, creature->GetObjectGuid());
        return true;
    }

    void SendHandInMenu(Player* player, Creature* creature, NpcBinding const& binding)
    {
        if (!player || !creature)
            return;

        CraftingSession* session = sCraftingOrders.GetSession(player);
        if (!session || session->creatureGuid != creature->GetGUIDLow() ||
            session->professionId != binding.professionId ||
            session->service == CraftingOrdersDomain::SERVICE_DISENCHANT)
        {
            player->CLOSE_GOSSIP_MENU();
            return;
        }

        HandInGossipState state;
        state.creatureGuid = creature->GetGUIDLow();
        state.professionId = binding.professionId;

        std::vector<std::string> records = sCraftingOrders.BuildHandInRecords(
            player, binding.professionId);
        for (std::string const& record : records)
        {
            CraftingOrdersDomain::HandInRecord entry =
                CraftingOrdersDomain::ParseHandInRecord(record);
            if (!entry.valid || !entry.taughtSpell)
                continue;
            if (state.entries.size() >= 100 ||
                state.entries.size() + 1 >= GOSSIP_MAX_MENU_ITEMS)
                break;
            state.entries.push_back(entry);
        }

        HandInGossipStates[player->GetGUIDLow()] = state;
        player->PlayerTalkClass->ClearMenus();

        if (state.entries.empty())
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT,
                "You have no new recipes to hand in.", CRAFTING_GOSSIP_SENDER,
                ACTION_BACK_MAIN);
        }
        else
        {
            for (size_t i = 0; i < state.entries.size(); ++i)
            {
                CraftingOrdersDomain::HandInRecord const& entry = state.entries[i];
                std::string label = entry.name.empty() ? "Unknown Recipe" : entry.name;
                RecipeData const* recipe = sCraftingOrders.GetRecipeForSpell(entry.taughtSpell);
                ItemPrototype const* result = recipe
                    ? sObjectMgr.GetItemPrototype(recipe->createdItemId) : nullptr;
                if (result)
                    label += " -> " + std::string(result->Name1);

                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, label.c_str(),
                    CRAFTING_GOSSIP_SENDER, ACTION_HANDIN_BASE + uint32(i));
            }
        }

        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Back", CRAFTING_GOSSIP_SENDER,
            ACTION_BACK_MAIN);
        player->SEND_GOSSIP_MENU(GOSSIP_TEXT_ID, creature->GetObjectGuid());
    }

    void ClearHandInState(Player* player)
    {
        if (player)
        {
            HandInGossipStates.erase(player->GetGUIDLow());
            NativeTrainerMenus.erase(player->GetGUIDLow());
        }
    }

    bool HandleHandInSelection(Player* player, Creature* creature, uint32 action)
    {
        if (!player || !creature || !IsHandInRecipeAction(action))
            return false;

        auto it = HandInGossipStates.find(player->GetGUIDLow());
        uint32 index = action - ACTION_HANDIN_BASE;
        if (it == HandInGossipStates.end() ||
            it->second.creatureGuid != creature->GetGUIDLow() ||
            it->second.professionId == 0 || index >= it->second.entries.size())
        {
            HandInGossipStates.erase(player->GetGUIDLow());
            player->CLOSE_GOSSIP_MENU();
            return true;
        }

        uint32 itemGuidLow = it->second.entries[index].itemGuid;
        std::string result;
        sCraftingOrders.HandInRecipe(player, itemGuidLow, result);
        HandInGossipStates.erase(it);
        player->CLOSE_GOSSIP_MENU();
        if (player->GetSession() && !result.empty())
            ChatHandler(player->GetSession()).SendSysMessage(result);
        return true;
    }

    // Runtime-discovered profession trainers must be augmented after the
    // core has run the creature's own gossip script.  Adding these entries
    // from CanCreatureGossipHello short-circuits scripted trainers and lets
    // their script clear the menu again, which makes the options disappear.
    bool AppendTrainerMenu(Player* player, Creature* creature, NpcBinding const& binding)
    {
        if (!player || !player->PlayerTalkClass || !creature || !sCraftingOrdersConfig.TrainerGossipEnabled())
            return false;

        CacheNativeTrainerMenu(player, creature);
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        auto hasAction = [&](uint32 action)
        {
            for (unsigned int item = 0; item < menu.MenuItemCount(); ++item)
                if (menu.MenuItemSender(item) == CRAFTING_GOSSIP_SENDER &&
                    menu.MenuItemAction(item) == action)
                    return true;
            return false;
        };

        bool changed = false;
        auto append = [&](uint8 icon, char const* text, uint32 action)
        {
            if (hasAction(action) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
                return;
            menu.AddMenuItem(icon, text, CRAFTING_GOSSIP_SENDER, action);
            // GossipMenu stores action metadata in a parallel vector. Keep it
            // aligned so native selections remain safe if this menu is later
            // handled by the core rather than our packet hook.
            menu.AddGossipMenuItemData(0, 0, 0);
            changed = true;
        };

        if (binding.service == CraftingOrdersDomain::SERVICE_DISENCHANT)
            append(GOSSIP_ICON_INTERACT_1, "Disenchant an item", ACTION_BROWSE);
        else if (binding.service == CraftingOrdersDomain::SERVICE_ENCHANT)
        {
            append(GOSSIP_ICON_TRAINER, "Browse enchants", ACTION_BROWSE);
            if (sCraftingOrdersConfig.EnchantingDisenchantEnabled())
                append(GOSSIP_ICON_INTERACT_1, "Disenchant an item", ACTION_DISENCHANT);
            append(GOSSIP_ICON_INTERACT_1, "Hand in a formula", ACTION_HANDIN);
        }
        else
        {
            append(GOSSIP_ICON_TRAINER, "Browse recipes", ACTION_BROWSE);
            append(GOSSIP_ICON_INTERACT_1, "Hand in a recipe", ACTION_HANDIN);
        }
        return changed;
    }

    bool HandleSelect(Player* player, Creature* creature, uint32 action)
    {
        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return false;

        if (IsHandInRecipeAction(action))
            return HandleHandInSelection(player, creature, action);

        if (action == ACTION_BACK_MAIN)
        {
            // Runtime-discovered trainers still need their native gossip
            // options (especially "Train me") when returning from hand-in.
            // Dedicated crafting NPCs intentionally remain module-only.
            bool preserveDefaultMenu = !sCraftingOrders.GetNpcBinding(creature->GetEntry());
            SendMainMenu(player, creature, preserveDefaultMenu);
            return true;
        }

        if (!IsCraftingGossipAction(action))
        {
            player->CLOSE_GOSSIP_MENU();
            return false;
        }

        uint32 serviceOverride = action == ACTION_DISENCHANT &&
            binding.professionId == CraftingOrdersDomain::PROF_ENCHANTING
            ? CraftingOrdersDomain::SERVICE_DISENCHANT : 0;
        if (!sCraftingOrders.OpenSession(player, creature, serviceOverride))
            return false;

        if (action == ACTION_BROWSE)
        {
            SendInit(player, &binding, CraftingOrdersDomain::UI_MODE_BROWSE);
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        if (action == ACTION_HANDIN)
        {
            SendHandInMenu(player, creature, binding);
            return true;
        }
        if (action == ACTION_DISENCHANT && binding.professionId == CraftingOrdersDomain::PROF_ENCHANTING &&
            sCraftingOrdersConfig.EnchantingDisenchantEnabled())
        {
            // Open a dedicated disenchant session and select that view in the
            // addon. The player can return to the trainer for direct enchants.
            SendInit(player, &binding, CraftingOrdersDomain::UI_MODE_BROWSE,
                CraftingOrdersDomain::SERVICE_DISENCHANT);
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        player->CLOSE_GOSSIP_MENU();
        return false;
    }

    // The core has no AllCreatureScript select hook.  Consume only our own
    // dynamically-added actions before the normal gossip handler runs.
    bool HandleDynamicGossipSelect(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || packet.GetOpcode() != CMSG_GOSSIP_SELECT_OPTION || packet.size() < sizeof(ObjectGuid) + sizeof(uint32))
            return true;

        Player* player = session->GetPlayer();
        if (!player)
            return true;

        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        uint32 gossipListId = 0;
        copy >> guid >> gossipListId;
        if (!guid.IsAnyTypeCreature())
            return true;
        if (session->GetCurrentGossipGUID() != guid)
            return true;

        Creature* creature = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (!creature)
            return true;

        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return true;
        if (!sCraftingOrders.GetNpcBinding(creature->GetEntry()) &&
            !sCraftingOrdersConfig.TrainerGossipEnabled())
            return true;
        if (gossipListId >= player->PlayerTalkClass->GetGossipMenu().MenuItemCount())
            return true;
        if (player->PlayerTalkClass->GossipOptionSender(gossipListId) != CRAFTING_GOSSIP_SENDER)
            return true;

        uint32 action = player->PlayerTalkClass->GossipOptionAction(gossipListId);
        if (!IsCraftingGossipAction(action))
            return true;

        // HandleSelect re-opens the session after all packet/guid checks above,
        // preserving the same short-lived NPC-bound session semantics used by
        // explicitly bound crafting NPCs.
        sLog.outDebug("[mod-crafting-orders] Dynamic gossip select player=%u entry=%u index=%u action=%u",
            player->GetGUIDLow(), creature->GetEntry(), gossipListId, action);
        return !HandleSelect(player, creature, action);
    }

    void HandleDynamicGossipHello(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || packet.GetOpcode() != CMSG_GOSSIP_HELLO || !sCraftingOrders.Enabled() ||
            !sCraftingOrdersConfig.TrainerGossipEnabled())
            return;

        Player* player = session->GetPlayer();
        if (!player || !player->PlayerTalkClass)
            return;

        ObjectGuid const guid = session->GetCurrentGossipGUID();
        Creature* creature = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (!creature || sCraftingOrders.GetNpcBinding(creature->GetEntry()))
            return;

        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return;

        // This hook runs after the normal NPC/script gossip handler, so the
        // trainer's existing options are already present. Resend the same
        // menu only after our entries were appended.
        if (AppendTrainerMenu(player, creature, binding))
        {
            sLog.outDebug("[mod-crafting-orders] Appended trainer gossip player=%u entry=%u profession=%u menuItems=%u",
                player->GetGUIDLow(), creature->GetEntry(), binding.professionId,
                player->PlayerTalkClass->GetGossipMenu().MenuItemCount());
            player->SendPreparedGossip(creature);
        }
    }
}

class crafting_orders_worldscript : public WorldScript
{
public:
    crafting_orders_worldscript() : WorldScript("crafting_orders_worldscript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_UPDATE
    }) {}

    void OnAfterConfigLoad(bool reload) override
    {
        sCraftingOrdersConfig.Load();
        if (reload)
            sCraftingOrders.Load();
        if (!sCraftingOrdersConfig.Enabled())
            sLog.outString("[mod-crafting-orders] Module disabled.");
    }

    void OnStartup() override
    {
        sCraftingOrders.Load();
    }

    void OnUpdate(uint32 diff) override
    {
        sCraftingOrders.Update(diff);
    }
};

class crafting_orders_playerscript : public PlayerScript
{
public:
    crafting_orders_playerscript() : PlayerScript("crafting_orders_playerscript", {
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_BEFORE_TELEPORT
    }) {}

    void OnLogout(Player* player) override
    {
        ClearHandInState(player);
        sCraftingOrders.CloseSession(player);
    }

    void OnMapChanged(Player* player) override
    {
        ClearHandInState(player);
        sCraftingOrders.CloseSession(player);
    }

    void OnBeforeTeleport(Player* player, uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/) override
    {
        ClearHandInState(player);
        sCraftingOrders.CloseSession(player);
    }
};

class crafting_orders_creature : public CreatureScript
{
public:
    explicit crafting_orders_creature(char const* name) : CreatureScript(name) {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sCraftingOrders.Enabled())
            return false;
        return SendMainMenu(player, creature);
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (!sCraftingOrders.Enabled())
            return false;
        return HandleSelect(player, creature, action);
    }
};

class crafting_orders_all_creature : public AllCreatureScript
{
public:
    crafting_orders_all_creature() : AllCreatureScript("crafting_orders_all_creature") {}

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!sCraftingOrders.Enabled() || !creature)
            return false;
        if (sCraftingOrders.GetNpcBinding(creature->GetEntry()))
            return SendMainMenu(player, creature);

        // Runtime-discovered trainers are appended from the server packet
        // hook after the core has finished the creature-specific gossip.
        // Returning true here would short-circuit scripted trainer gossip.
        return false;
    }
};

class crafting_orders_server : public ServerScript
{
public:
    crafting_orders_server() : ServerScript("crafting_orders_server", {
        SERVERHOOK_CAN_PACKET_RECEIVE,
        SERVERHOOK_ON_PACKET_HANDLED
    }) {}

    void OnPacketHandled(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() == CMSG_GOSSIP_HELLO)
            HandleDynamicGossipHello(session, packet);
        else if (packet.GetOpcode() == CMSG_GOSSIP_SELECT_OPTION)
        {
            // Normally the pre-handler consumes our option. Keep a narrow
            // post-handler fallback for cores/scripts that allow the packet
            // through first; native options still never match our sender.
            HandleDynamicGossipSelect(session, packet);
        }
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!sCraftingOrders.HandleAddonPacket(session, packet))
            return false;
        return HandleDynamicGossipSelect(session, packet);
    }
};

void AddSC_crafting_orders_scripts()
{
    new crafting_orders_worldscript();
    new crafting_orders_playerscript();
    new crafting_orders_creature("crafting_order");
    new crafting_orders_creature("crafting_order_enchant");
    new crafting_orders_creature("crafting_order_disenchant");
    new crafting_orders_all_creature();
    new crafting_orders_server();
}
