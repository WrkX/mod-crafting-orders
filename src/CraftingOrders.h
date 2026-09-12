#ifndef CRAFTING_ORDERS_H
#define CRAFTING_ORDERS_H

#include "Common.h"
#include "CraftingOrdersDomain.h"
#include "ObjectGuid.h"
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

class Player;
class Item;
class Creature;
class WorldSession;
class WorldPacket;
struct SpellEntry;
struct ItemPrototype;

struct CraftMaterial
{
    uint32 itemId = 0;
    uint32 count = 0;
};

struct RecipeData
{
    uint32 spellId = 0;
    uint32 professionId = 0;
    uint32 createdItemId = 0;
    uint32 createdItemCount = 1;
    uint32 skillTier = 0;
    uint32 reqSkillRank = 0;
    uint32 reqLevel = 0;
    uint32 spellCooldownSecs = 0;
    uint32 spellCategory = 0;
    float goldFeeMultiplier = 1.0f;
    bool isEnchantSpell = false;
    bool isPermanentEnchant = true;
    uint32 enchantId = 0;
    uint32 source = CraftingOrdersDomain::RECIPE_SOURCE_TRAINER;
    bool requiresUnlock = false;
    std::string displayName;
    std::vector<CraftMaterial> materials;
};

struct NpcBinding
{
    uint32 entry = 0;
    uint32 professionId = 0;
    uint32 service = CraftingOrdersDomain::SERVICE_NONE;
    std::string scriptName;
    bool available = false;
};

struct CraftingSession
{
    uint32 playerGuid = 0;
    uint32 creatureGuid = 0;
    uint32 creatureEntry = 0;
    uint32 mapId = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    uint32 professionId = 0;
    uint32 service = CraftingOrdersDomain::SERVICE_NONE;
    uint32 createdMs = 0;
    uint32 expiresMs = 0;
    uint32 lastRequestId = 0;
    uint32 windowStartMs = 0;
    uint32 windowCount = 0;
    bool busy = false;
};

class CraftingOrdersConfig
{
public:
    static CraftingOrdersConfig& Instance();
    CraftingOrdersConfig(CraftingOrdersConfig const&) = delete;
    CraftingOrdersConfig& operator=(CraftingOrdersConfig const&) = delete;

    void Load();
    bool Enabled() const { return _enabled; }
    bool TrainerGossipEnabled() const { return _trainerGossipEnabled; }
    bool EnchantingDisenchantEnabled() const { return _enchantingDisenchantEnabled; }

    CraftingOrdersDomain::FeeConfig const& Fees() const { return _fees; }
    bool EnforceCooldowns() const { return _enforceCooldowns; }
    bool AccountWideCooldowns() const { return _accountWideCooldowns; }
    bool DisenchantEnabled() const { return _disenchantEnabled; }
    uint32 MaxQuantity() const { return _maxQuantity; }
    uint32 MinFeeCopper() const { return _fees.minFeeCopper; }
    std::vector<uint32> const& AllowList() const { return _allow; }
    std::vector<uint32> const& DenyList() const { return _deny; }
    float RecipeMultiplier(uint32 professionId, uint32 spellId) const;

private:
    CraftingOrdersConfig() = default;
    bool _enabled = false;
    bool _trainerGossipEnabled = true;
    bool _enchantingDisenchantEnabled = true;
    bool _enforceCooldowns = true;
    bool _accountWideCooldowns = false;
    bool _disenchantEnabled = true;
    uint32 _maxQuantity = CraftingOrdersDomain::DEFAULT_MAX_QUANTITY;
    CraftingOrdersDomain::FeeConfig _fees;
    std::vector<uint32> _allow;
    std::vector<uint32> _deny;
    std::map<std::pair<uint32, uint32>, float> _overrides;
};

class CraftingOrders
{
public:
    static CraftingOrders& Instance();
    CraftingOrders(CraftingOrders const&) = delete;
    CraftingOrders& operator=(CraftingOrders const&) = delete;

    void Load();
    void Update(uint32 diff);
    bool Enabled() const;

    NpcBinding const* GetNpcBinding(uint32 creatureEntry) const;
    bool ResolveNpcBinding(Creature const* creature, NpcBinding& binding) const;
    RecipeData const* GetRecipeForSpell(uint32 spellId) const;
    std::vector<RecipeData> GetAvailableRecipes(Player* player, uint32 professionId) const;
    uint32 CalculateGoldFee(RecipeData const& recipe) const;
    bool ValidateMaterials(RecipeData const& recipe, Player* player, uint32 quantity, std::string& error) const;
    bool IsOnCooldown(Player* player, uint32 spellId) const;
    void SetCooldown(Player* player, uint32 spellId, uint32 cooldownSecs);
    bool HasPlayerRecipe(Player* player, uint32 professionId, uint32 spellId) const;
    bool AddPlayerRecipe(Player* player, uint32 professionId, uint32 spellId);
    RecipeData const* ResolveRecipeItem(ItemPrototype const* proto, uint32 professionId, uint32& taughtSpell) const;
    bool IsEnchantmentSpell(SpellEntry const* spellInfo, uint32* enchantId = nullptr, bool* permanent = nullptr) const;

    bool OpenSession(Player* player, Creature* creature, uint32 serviceOverride = 0);
    CraftingSession* GetSession(Player* player);
    bool ValidateSession(Player* player, uint32 expectedService, std::string& error);
    void CloseSession(uint32 playerGuid);
    void CloseSession(Player* player);

    bool HandleAddonPacket(WorldSession* session, WorldPacket const& packet);
    void SendAddon(Player* player, uint32 requestId, std::string const& opcode, std::string const& payload, uint32 page = 0, uint32 totalPages = 1);

    std::vector<std::string> BuildRecipeRecords(Player* player, uint32 professionId, std::string const& filter, uint32 tier) const;
    std::vector<std::string> BuildDisenchantRecords(Player* player) const;
    std::vector<std::string> BuildHandInRecords(Player* player, uint32 professionId) const;
    std::string BuildRecipeDataMessage(Player* player, uint32 professionId, std::string const& filter, uint32 tier) const;
    std::string BuildDisenchantDataMessage(Player* player) const;
    std::string BuildHandInDataMessage(Player* player, uint32 professionId) const;

    static std::vector<uint32> const& GetEnchantEquipmentSlots();
    static std::string GetSlotName(uint32 slot);

private:
    CraftingOrders() = default;

    void LoadNpcBindings();
    void LoadRecipes();
    void CleanupExpiredCooldowns();
    bool MakeRecipeData(uint32 spellId, uint32 skillLine, uint32 skillRank, uint32 reqLevel, RecipeData& rd) const;
    uint32 ResolveCraftSpell(uint32 trainerSpellId) const;
    uint32 GetNowMs() const;
    bool RateLimit(CraftingSession& session, uint32 nowMs);
    Creature* ResolveSessionCreature(Player* player, CraftingSession const& session) const;
    Item* FindOwnedItem(Player* player, uint32 bag, uint32 slot, uint32 expectedEntry = 0) const;
    void ForEachInventoryItem(Player* player, std::function<void(Item*)> const& fn) const;

    bool Craft(Player* player, uint32 spellId, uint32 quantity, std::string& result);
    bool Enchant(Player* player, uint32 spellId, uint32 bag, uint32 slot, std::string& result);
    bool Disenchant(Player* player, uint32 bag, uint32 slot, std::string& result);
    bool HandIn(Player* player, uint32 itemGuidLow, std::string& result);
    bool PlayerCanUseRecipe(Player* player, RecipeData const& recipe) const;
    void MailDisenchantRecovery(Player* player, std::vector<Item*>& items, uint32 npcEntry) const;

    std::unordered_map<uint32, std::vector<RecipeData>> _trainerRecipes;
    std::unordered_map<uint32, RecipeData> _spellRecipeMap;
    std::unordered_map<uint32, NpcBinding> _npcBindings;
    std::unordered_map<uint32, CraftingSession> _sessions;
    uint32 _elapsedMs = 0;
    uint32 _nextCleanupMs = 0;
    bool _loaded = false;
    bool _disabledForData = false;
};

#define sCraftingOrdersConfig CraftingOrdersConfig::Instance()
#define sCraftingOrders CraftingOrders::Instance()

void AddSC_crafting_orders_scripts();

#endif
