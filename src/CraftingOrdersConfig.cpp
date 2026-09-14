#include "CraftingOrders.h"
#include "Config/Config.h"
#include "Log.h"
#include <algorithm>
#include <cmath>

namespace
{
    float NonNegativeFinite(float value)
    {
        return std::isfinite(value) && value >= 0.0f ? value : 0.0f;
    }
}

CraftingOrdersConfig& CraftingOrdersConfig::Instance()
{
    static CraftingOrdersConfig instance;
    return instance;
}

float CraftingOrdersConfig::RecipeMultiplier(uint32 professionId, uint32 spellId) const
{
    auto it = _overrides.find(std::make_pair(professionId, spellId));
    if (it != _overrides.end())
        return it->second;
    return 1.0f;
}

void CraftingOrdersConfig::Load()
{
    _enabled = sConfig.GetBoolDefault("CraftingOrders.Enable", false);
    _trainerGossipEnabled = sConfig.GetBoolDefault("CraftingOrders.TrainerGossip.Enable", true);
    _enchantingDisenchantEnabled = sConfig.GetBoolDefault("CraftingOrders.TrainerGossip.EnchantingDisenchant", true);
    int32 craftingTimeMinutes = sConfig.GetIntDefault("CraftingOrders.CraftingTimeMinutes", 0);
    uint32 const maxCraftingTimeMinutes = 0xFFFFFFFFu / 60u;
    _craftingTimeMinutes = craftingTimeMinutes < 0 ? 0 :
        std::min(uint32(craftingTimeMinutes), maxCraftingTimeMinutes);
    _enforceCooldowns = sConfig.GetBoolDefault("CraftingOrders.EnforceCooldowns", true);
    _accountWideCooldowns = sConfig.GetBoolDefault("CraftingOrders.AccountWideCooldowns", false);
    _disenchantEnabled = sConfig.GetBoolDefault("CraftingOrders.Disenchant.Enable", true);

    int32 maxQuantity = sConfig.GetIntDefault("CraftingOrders.MaxQuantity", int32(CraftingOrdersDomain::DEFAULT_MAX_QUANTITY));
    if (maxQuantity < 1)
        maxQuantity = 1;
    if (maxQuantity > int32(CraftingOrdersDomain::MAX_QUANTITY_HARD))
        maxQuantity = int32(CraftingOrdersDomain::MAX_QUANTITY_HARD);
    _maxQuantity = uint32(maxQuantity);

    _fees.defaultFeePercent = NonNegativeFinite(
        sConfig.GetFloatDefault("CraftingOrders.DefaultFeePercent", 1.0f));
    int32 enchantFee = sConfig.GetIntDefault("CraftingOrders.EnchantFeePerSkillPoint", int32(CraftingOrdersDomain::SILVER));
    _fees.enchantFeePerSkillPoint = enchantFee < 0 ? 0 : uint32(enchantFee);
    int32 minFee = sConfig.GetIntDefault("CraftingOrders.MinFeeCopper", int32(CraftingOrdersDomain::MIN_FEE_DEFAULT));
    _fees.minFeeCopper = minFee < 0 ? 0 : uint32(minFee);

    _fees.tierMultiplier[CraftingOrdersDomain::TIER_APPRENTICE] = NonNegativeFinite(
        sConfig.GetFloatDefault("CraftingOrders.FeeMultiplier.Apprentice", 1.0f));
    _fees.tierMultiplier[CraftingOrdersDomain::TIER_JOURNEYMAN] = NonNegativeFinite(
        sConfig.GetFloatDefault("CraftingOrders.FeeMultiplier.Journeyman", 2.0f));
    _fees.tierMultiplier[CraftingOrdersDomain::TIER_EXPERT] = NonNegativeFinite(
        sConfig.GetFloatDefault("CraftingOrders.FeeMultiplier.Expert", 3.0f));
    _fees.tierMultiplier[CraftingOrdersDomain::TIER_ARTISAN] = NonNegativeFinite(
        sConfig.GetFloatDefault("CraftingOrders.FeeMultiplier.Artisan", 4.0f));

    _fees.disenchantFees = CraftingOrdersDomain::ParseFeeRanges(
        sConfig.GetStringDefault("CraftingOrders.Disenchant.Fees", "0-50:100;51-100:400;101-150:1000;151-999:2500"));
    int32 deDefault = sConfig.GetIntDefault("CraftingOrders.Disenchant.DefaultFee", int32(CraftingOrdersDomain::GOLD));
    _fees.disenchantDefaultFee = deDefault < 0 ? 0 : uint32(deDefault);

    _overrides.clear();
    for (CraftingOrdersDomain::RecipeOverride const& ov :
         CraftingOrdersDomain::ParseRecipeOverrides(sConfig.GetStringDefault("CraftingOrders.RecipeOverrides", "")))
        _overrides[{ov.professionId, ov.spellId}] = ov.multiplier;

    _allow = CraftingOrdersDomain::ParseIdList(sConfig.GetStringDefault("CraftingOrders.RecipeAllow", ""));
    _deny = CraftingOrdersDomain::ParseIdList(sConfig.GetStringDefault("CraftingOrders.RecipeDeny", ""));

    sLog.outString("[mod-crafting-orders] Config loaded (enable=%u, craftingTimeMinutes=%u, maxQuantity=%u, overrides=%u, allow=%u, deny=%u)",
        uint32(_enabled), _craftingTimeMinutes, _maxQuantity, uint32(_overrides.size()), uint32(_allow.size()), uint32(_deny.size()));
}
