#include "CraftingOrders.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
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
        ACTION_DISENCHANT = GOSSIP_ACTION_INFO_DEF + 3
    };

    constexpr uint32 GOSSIP_TEXT_ID = 1;
    // Keep module actions separate from the conventional MAIN sender. This
    // prevents a coincidental action value in an unrelated trainer script
    // from being consumed by the packet hook.
    constexpr uint32 CRAFTING_GOSSIP_SENDER = 0x4352;

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

        if (preserveDefaultMenu)
            player->PrepareGossipMenu(creature, creature->GetDefaultGossipMenuId());
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

    bool HandleSelect(Player* player, Creature* creature, uint32 action)
    {
        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return false;
        if (action != ACTION_BROWSE && action != ACTION_HANDIN && action != ACTION_DISENCHANT)
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
            SendInit(player, &binding, CraftingOrdersDomain::UI_MODE_HANDIN);
            player->CLOSE_GOSSIP_MENU();
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
        if (action != ACTION_BROWSE && action != ACTION_HANDIN && action != ACTION_DISENCHANT)
            return true;

        // HandleSelect re-opens the session after all packet/guid checks above,
        // preserving the same short-lived NPC-bound session semantics used by
        // explicitly bound crafting NPCs.
        return !HandleSelect(player, creature, action);
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
        sCraftingOrders.CloseSession(player);
    }

    void OnMapChanged(Player* player) override
    {
        sCraftingOrders.CloseSession(player);
    }

    void OnBeforeTeleport(Player* player, uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/) override
    {
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

        if (!sCraftingOrdersConfig.TrainerGossipEnabled())
            return false;

        NpcBinding binding;
        if (!sCraftingOrders.ResolveNpcBinding(creature, binding))
            return false;
        // Prepare the core's normal trainer/quest/vendor menu, then append the
        // crafting-order entries for this profession trainer.
        // Do not establish an addon session merely because the normal trainer
        // gossip was opened. A session is created only after the player picks
        // one of our options; this prevents a native trainer selection from
        // inheriting a stale crafting session.
        sCraftingOrders.CloseSession(player);
        return SendMainMenu(player, creature, true, false);
    }
};

class crafting_orders_server : public ServerScript
{
public:
    crafting_orders_server() : ServerScript("crafting_orders_server", { SERVERHOOK_CAN_PACKET_RECEIVE }) {}

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
