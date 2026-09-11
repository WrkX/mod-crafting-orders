#include "CraftingOrders.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
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
        ACTION_HANDIN = GOSSIP_ACTION_INFO_DEF + 2
    };

    constexpr uint32 GOSSIP_TEXT_ID = 1;

    void SendInit(Player* player, NpcBinding const* binding, uint32 uiMode)
    {
        std::string payload = std::to_string(binding->professionId) + "\t" +
            CraftingOrdersDomain::ProfessionName(binding->professionId) + "\t" +
            std::to_string(binding->service) + "\t" +
            std::to_string(uiMode);
        sCraftingOrders.SendAddon(player, 0, "INIT", payload);
    }

    bool SendMainMenu(Player* player, Creature* creature)
    {
        NpcBinding const* binding = sCraftingOrders.GetNpcBinding(creature->GetEntry());
        if (!binding)
            return false;
        if (!sCraftingOrders.OpenSession(player, creature))
            return false;

        player->PlayerTalkClass->ClearMenus();
        if (binding->service == CraftingOrdersDomain::SERVICE_DISENCHANT)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Disenchant an item", GOSSIP_SENDER_MAIN, ACTION_BROWSE);
        else if (binding->service == CraftingOrdersDomain::SERVICE_ENCHANT)
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, "Browse enchants", GOSSIP_SENDER_MAIN, ACTION_BROWSE);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Hand in a formula", GOSSIP_SENDER_MAIN, ACTION_HANDIN);
        }
        else
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, "Browse recipes", GOSSIP_SENDER_MAIN, ACTION_BROWSE);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Hand in a recipe", GOSSIP_SENDER_MAIN, ACTION_HANDIN);
        }
        player->SEND_GOSSIP_MENU(GOSSIP_TEXT_ID, creature->GetObjectGuid());
        return true;
    }

    bool HandleSelect(Player* player, Creature* creature, uint32 action)
    {
        NpcBinding const* binding = sCraftingOrders.GetNpcBinding(creature->GetEntry());
        if (!binding)
            return false;
        if (!sCraftingOrders.OpenSession(player, creature))
            return false;

        if (action == ACTION_BROWSE)
        {
            SendInit(player, binding, CraftingOrdersDomain::UI_MODE_BROWSE);
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        if (action == ACTION_HANDIN)
        {
            SendInit(player, binding, CraftingOrdersDomain::UI_MODE_HANDIN);
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        player->CLOSE_GOSSIP_MENU();
        return false;
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
        if (!sCraftingOrders.GetNpcBinding(creature->GetEntry()))
            return false;
        return SendMainMenu(player, creature);
    }
};

class crafting_orders_server : public ServerScript
{
public:
    crafting_orders_server() : ServerScript("crafting_orders_server", { SERVERHOOK_CAN_PACKET_RECEIVE }) {}

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        return sCraftingOrders.HandleAddonPacket(session, packet);
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
