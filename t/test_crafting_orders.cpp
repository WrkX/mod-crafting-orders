#include "CraftingOrdersDomain.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace CraftingOrdersDomain;

static int failures = 0;

static void Expect(bool cond, char const* name)
{
    if (!cond)
    {
        std::cerr << "FAIL " << name << std::endl;
        ++failures;
    }
    else
        std::cout << "ok   " << name << std::endl;
}

int main()
{
    Expect(GetSkillTier(1) == TIER_APPRENTICE, "tier apprentice");
    Expect(GetSkillTier(75) == TIER_APPRENTICE, "tier apprentice cap");
    Expect(GetSkillTier(76) == TIER_JOURNEYMAN, "tier journeyman");
    Expect(GetSkillTier(225) == TIER_EXPERT, "tier expert");
    Expect(GetSkillTier(226) == TIER_ARTISAN, "tier artisan");
    Expect(GetSkillTier(300) == TIER_ARTISAN, "tier artisan cap");
    Expect(GetSkillTier(301) == TIER_NONE, "tier above artisan rejected");
    Expect(GetSkillTier(0) == TIER_NONE, "tier zero rejected");

    uint32 out = 0;
    Expect(CheckedMulU32(20, 20, out) && out == 400, "mul ok");
    Expect(!CheckedMulU32(3000000000u, 3u, out), "mul overflow");
    Expect(CheckedAddU32(1, 2, out) && out == 3, "add ok");
    Expect(!CheckedAddU32(0xFFFFFFFFu, 1u, out), "add overflow");

    uint32 qty = 0;
    Expect(ClampQuantity(1, 20, qty) && qty == 1, "qty min");
    Expect(ClampQuantity(20, 20, qty) && qty == 20, "qty max");
    Expect(!ClampQuantity(0, 20, qty), "qty zero rejected");
    Expect(!ClampQuantity(21, 20, qty), "qty over max rejected");

    FeeConfig fees;
    RecipeFeeInput input;
    input.createdItemSellPrice = 1000;
    input.skillTier = TIER_ARTISAN;
    input.recipeMultiplier = 1.0f;
    uint32 fee = CalculateRecipeFee(input, fees);
    Expect(fee == 4000, "sell-price fee with artisan multiplier");

    input.createdItemSellPrice = 0;
    input.reagentSellTotal = 0;
    fee = CalculateRecipeFee(input, fees);
    Expect(fee == MIN_FEE_DEFAULT, "min fee for unsellable item");

    input.professionId = PROF_ENCHANTING;
    input.isEnchantSpell = true;
    input.reqSkillRank = 250;
    input.createdItemId = 0;
    fees.enchantFeePerSkillPoint = 100;
    fee = CalculateRecipeFee(input, fees);
    Expect(fee == 100000, "enchant fee per skill * artisan");

    auto ranges = ParseFeeRanges("0-50:100;51-100:400;bad;101-150:1000");
    Expect(ranges.size() == 3, "parse three fee ranges");
    Expect(FeeForItemLevel(ranges, 25, 999) == 100, "fee low ilvl");
    Expect(FeeForItemLevel(ranges, 80, 999) == 400, "fee mid ilvl");
    Expect(FeeForItemLevel(ranges, 200, 777) == 777, "fee default");

    auto ovs = ParseRecipeOverrides("3275,129,0.0;12044,197,2.5;nope");
    Expect(ovs.size() == 2, "parse overrides");
    Expect(ovs[0].multiplier == 0.0f, "override disable");

    auto allow = ParseIdList("10;20;10;30");
    auto deny = ParseIdList("20");
    Expect(allow.size() == 3, "unique allow ids");
    Expect(RecipeAllowed(10, allow, deny), "allow listed");
    Expect(!RecipeAllowed(20, allow, deny), "deny wins");
    Expect(!RecipeAllowed(99, allow, deny), "not in allow");
    Expect(RecipeAllowed(99, std::vector<uint32>(), deny), "empty allow exposes recipe");

    Expect(!ParseU32("", out), "empty u32");
    Expect(!ParseU32("12a", out), "bad u32");
    Expect(ParseU32("42", out) && out == 42, "good u32");
    Expect(!ParseU32("1000000001", out), "u32 over max");

    ProtocolRequest bad = ParseProtocolPayload("not-a-packet");
    Expect(!bad.valid, "reject short payload");
    ProtocolRequest huge = ParseProtocolPayload(std::string(600, 'A'));
    Expect(!huge.valid, "reject oversized payload");
    ProtocolRequest ok = ParseProtocolPayload("2\t7\tCRAFT\t12345\t2");
    Expect(ok.valid && ok.version == 2 && ok.requestId == 7 && ok.opcode == "CRAFT", "parse craft");
    Expect(ok.fields.size() >= 2 && ok.fields[0] == "12345", "craft fields");
    ProtocolRequest badOp = ParseProtocolPayload("2\t1\tcraft");
    Expect(!badOp.valid, "reject lowercase opcode");
    Expect(ADDON_PROTOCOL_VERSION == 2, "addon protocol version is 2");
#ifdef PROTOCOL_VERSION
    Expect(ADDON_PROTOCOL_VERSION != uint32(PROTOCOL_VERSION) || ADDON_PROTOCOL_VERSION == 2,
        "addon protocol name is not the MySQL macro");
#endif

    Expect(ReplayRejected(5, 5), "replay same id");
    Expect(ReplayRejected(5, 4), "replay older id");
    Expect(!ReplayRejected(5, 6), "accept newer id");

    std::string escaped = EscapeField("a|b,c;d");
    Expect(UnescapeField(escaped) == "a|b,c;d", "escape roundtrip delimiters");
    std::string slashName = EscapeField("back\\slash|pipe");
    Expect(UnescapeField(slashName) == "back\\slash|pipe", "escape roundtrip backslash");
    Expect(UnescapeField("a\\|b") == "a|b", "unescape escaped pipe");
    Expect(UnescapeField("x\\\\n") == "x\\n", "unescape escaped backslash then n");

    auto escapedParts = SplitUnescaped(EscapeField("a|b") + "|" + EscapeField("c,d") + "|" + EscapeField("e;f"), '|');
    Expect(escapedParts.size() == 3, "split unescaped keeps escaped pipes inside records");
    Expect(UnescapeField(escapedParts[0]) == "a|b", "record 0 unescapes pipe");
    Expect(UnescapeField(escapedParts[1]) == "c,d", "record 1 unescapes comma");
    Expect(UnescapeField(escapedParts[2]) == "e;f", "record 2 unescapes semicolon");

    std::vector<std::string> recs;
    for (int i = 0; i < 8; ++i)
        recs.push_back("item" + std::to_string(i));
    auto chunked = ChunkRecords(recs, 12);
    Expect(!chunked.overflow && !chunked.recordTooLarge, "chunk whole records");
    Expect(chunked.chunks.size() > 1, "multiple chunks for small size");
    Expect(AssembleChunks(chunked.chunks) == JoinRecords(recs), "reassembly does not insert extra separators");
    for (std::string const& chunk : chunked.chunks)
        Expect(chunk.size() <= 12 || chunk.back() == '|', "chunk stays on record boundary");

    auto tooBig = ChunkRecords(std::vector<std::string>{"abcdefghijklmnop"}, 8);
    Expect(tooBig.recordTooLarge && tooBig.chunks.empty(), "record larger than chunk is an error");

    std::vector<std::string> many;
    for (int i = 0; i < 20; ++i)
        many.push_back("rec" + std::to_string(i));
    auto overflowed = ChunkRecords(many, 8, 2);
    Expect(overflowed.overflow && overflowed.chunks.empty(), "overflow is an error not a silent truncate");

    auto page0 = PaginateRecords(many, 0, 8, 2);
    Expect(!page0.pageOutOfRange && !page0.recordTooLarge && page0.totalPages > 1, "paginate large list");
    auto pageLast = PaginateRecords(many, page0.totalPages - 1, 8, 2);
    Expect(!pageLast.pageOutOfRange && !pageLast.records.empty(), "last page has records");
    auto pageOob = PaginateRecords(many, page0.totalPages, 8, 2);
    Expect(pageOob.pageOutOfRange, "page out of range");

    std::string header = FormatProtocolHeader(ADDON_PROTOCOL_VERSION, 3, 1, 1, "RECIPES", 0, 2);
    Expect(header == "2\t3\t1\t1\tRECIPES\t0\t2", "versioned header includes page fields");

    Expect(RecipeEntitled(RECIPE_SOURCE_TRAINER, false), "trainer recipe needs no unlock");
    Expect(RecipeEntitled(RECIPE_SOURCE_TRAINER, true), "trainer recipe still entitled with unlock");
    Expect(!RecipeEntitled(RECIPE_SOURCE_FORMULA, false), "formula recipe locked without unlock");
    Expect(RecipeEntitled(RECIPE_SOURCE_FORMULA, true), "formula recipe entitled with unlock");

    Expect(std::string(ListActionOpcode(LIST_KIND_HANDIN)) == "HANDIN", "hand-in uses HANDIN opcode");
    Expect(std::string(ListActionOpcode(LIST_KIND_RECIPES)) == "CRAFT", "recipe list uses CRAFT opcode");
    Expect(ListActionUsesItemGuid(LIST_KIND_HANDIN), "hand-in action sends item guid");
    Expect(!ListActionUsesItemGuid(LIST_KIND_RECIPES), "recipe action does not send item guid");

    std::string handinEntry = std::to_string(4242) + ",12345," + EscapeField("Formula: A|B") + ",999,100";
    HandInRecord handin = ParseHandInRecord(handinEntry);
    Expect(handin.valid && handin.itemGuid == 4242 && handin.itemId == 12345, "parse hand-in guid not as spell");
    Expect(handin.name == "Formula: A|B", "hand-in name unescapes pipe");
    Expect(handin.taughtSpell == 999, "hand-in taught spell");
    Expect(!ParseHandInRecord("0,1,x").valid, "reject zero guid hand-in");
    Expect(!ParseHandInRecord("not-a-guid").valid, "reject malformed hand-in");

    Expect(IsSupportedProfession(PROF_JEWELCRAFTING), "turtle jewelcrafting");
    Expect(!IsSupportedProfession(773), "inscription deferred");

    if (failures)
    {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all tests passed" << std::endl;
    return 0;
}
