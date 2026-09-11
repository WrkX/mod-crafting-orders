#ifndef CRAFTING_ORDERS_DOMAIN_H
#define CRAFTING_ORDERS_DOMAIN_H

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace CraftingOrdersDomain
{
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    using int64 = std::int64_t;

    // Named to avoid colliding with MySQL's PROTOCOL_VERSION macro.
    constexpr uint32 ADDON_PROTOCOL_VERSION = 2;
    constexpr uint32 MAX_MESSAGE_SIZE = 512;
    constexpr uint32 MAX_FIELD_COUNT = 16;
    constexpr uint32 MAX_QUANTITY_HARD = 100;
    constexpr uint32 DEFAULT_MAX_QUANTITY = 20;
    constexpr uint32 MAX_NUMERIC_VALUE = 1000000000;
    constexpr uint32 MAX_CHUNK_PAYLOAD = 180;
    constexpr uint32 MAX_RESPONSE_CHUNKS = 64;
    constexpr uint32 SESSION_TTL_MS = 180000;
    constexpr uint32 RATE_LIMIT_WINDOW_MS = 1000;
    constexpr uint32 RATE_LIMIT_MAX = 8;
    constexpr uint32 ARTISAN_SKILL_CAP = 300;
    constexpr uint32 MIN_FEE_DEFAULT = 1;
    constexpr uint32 GOLD = 10000;
    constexpr uint32 SILVER = 100;

    enum ProfessionId : uint32
    {
        PROF_NONE = 0,
        PROF_BLACKSMITHING = 164,
        PROF_LEATHERWORKING = 165,
        PROF_ALCHEMY = 171,
        PROF_TAILORING = 197,
        PROF_ENGINEERING = 202,
        PROF_ENCHANTING = 333,
        PROF_JEWELCRAFTING = 755
    };

    enum ServiceType : uint32
    {
        SERVICE_NONE = 0,
        SERVICE_CRAFT = 1,
        SERVICE_ENCHANT = 2,
        SERVICE_DISENCHANT = 3
    };

    enum SkillTier : uint32
    {
        TIER_NONE = 0,
        TIER_APPRENTICE = 1,
        TIER_JOURNEYMAN = 2,
        TIER_EXPERT = 3,
        TIER_ARTISAN = 4
    };

    enum CooldownScope : uint32
    {
        COOLDOWN_SCOPE_CHARACTER = 0,
        COOLDOWN_SCOPE_ACCOUNT = 1
    };

    enum RecipeSource : uint32
    {
        RECIPE_SOURCE_TRAINER = 0,
        RECIPE_SOURCE_FORMULA = 1
    };

    enum UiMode : uint32
    {
        UI_MODE_BROWSE = 0,
        UI_MODE_HANDIN = 1
    };

    enum ListKind : uint32
    {
        LIST_KIND_RECIPES = 0,
        LIST_KIND_HANDIN = 1,
        LIST_KIND_DISENCHANT = 2,
        LIST_KIND_ENCHANT_TARGETS = 3
    };

    inline bool RecipeEntitled(uint32 source, bool hasAccountUnlock)
    {
        return source != RECIPE_SOURCE_FORMULA || hasAccountUnlock;
    }

    inline char const* ListActionOpcode(uint32 listKind)
    {
        switch (listKind)
        {
            case LIST_KIND_HANDIN: return "HANDIN";
            case LIST_KIND_DISENCHANT: return "DISENCHANT";
            case LIST_KIND_ENCHANT_TARGETS: return "ENCHANT";
            default: return "CRAFT";
        }
    }

    inline bool ListActionUsesItemGuid(uint32 listKind)
    {
        return listKind == LIST_KIND_HANDIN;
    }

    inline bool IsSupportedProfession(uint32 professionId)
    {
        switch (professionId)
        {
            case PROF_BLACKSMITHING:
            case PROF_LEATHERWORKING:
            case PROF_ALCHEMY:
            case PROF_TAILORING:
            case PROF_ENGINEERING:
            case PROF_ENCHANTING:
            case PROF_JEWELCRAFTING:
                return true;
            default:
                return false;
        }
    }

    inline char const* ProfessionName(uint32 professionId)
    {
        switch (professionId)
        {
            case PROF_BLACKSMITHING: return "Blacksmithing";
            case PROF_LEATHERWORKING: return "Leatherworking";
            case PROF_ALCHEMY: return "Alchemy";
            case PROF_TAILORING: return "Tailoring";
            case PROF_ENGINEERING: return "Engineering";
            case PROF_ENCHANTING: return "Enchanting";
            case PROF_JEWELCRAFTING: return "Jewelcrafting";
            default: return "Unknown";
        }
    }

    inline char const* ServiceName(uint32 service)
    {
        switch (service)
        {
            case SERVICE_CRAFT: return "Crafting";
            case SERVICE_ENCHANT: return "Enchanting";
            case SERVICE_DISENCHANT: return "Disenchanting";
            default: return "Unknown";
        }
    }

    inline uint32 GetSkillTier(uint32 reqSkillRank)
    {
        if (reqSkillRank == 0 || reqSkillRank > ARTISAN_SKILL_CAP)
            return TIER_NONE;
        if (reqSkillRank <= 75)
            return TIER_APPRENTICE;
        if (reqSkillRank <= 150)
            return TIER_JOURNEYMAN;
        if (reqSkillRank <= 225)
            return TIER_EXPERT;
        return TIER_ARTISAN;
    }

    inline bool CheckedAddU32(uint32 a, uint32 b, uint32& out)
    {
        uint64 const sum = uint64(a) + uint64(b);
        if (sum > std::numeric_limits<uint32>::max())
            return false;
        out = uint32(sum);
        return true;
    }

    inline bool CheckedMulU32(uint32 a, uint32 b, uint32& out)
    {
        uint64 const product = uint64(a) * uint64(b);
        if (product > std::numeric_limits<uint32>::max())
            return false;
        out = uint32(product);
        return true;
    }

    inline bool ClampQuantity(uint32 requested, uint32 maxQuantity, uint32& out)
    {
        if (requested == 0)
            return false;
        uint32 const cap = maxQuantity == 0 ? DEFAULT_MAX_QUANTITY : std::min(maxQuantity, MAX_QUANTITY_HARD);
        if (requested > cap)
            return false;
        out = requested;
        return true;
    }

    struct FeeRange
    {
        uint32 minIlvl = 0;
        uint32 maxIlvl = 0;
        uint32 copper = 0;
    };

    struct FeeConfig
    {
        float defaultFeePercent = 1.0f;
        uint32 enchantFeePerSkillPoint = SILVER;
        uint32 minFeeCopper = MIN_FEE_DEFAULT;
        float tierMultiplier[5] = { 1.0f, 1.0f, 2.0f, 3.0f, 4.0f }; // index by SkillTier
        std::vector<FeeRange> disenchantFees;
        uint32 disenchantDefaultFee = GOLD;
    };

    struct RecipeFeeInput
    {
        uint32 professionId = 0;
        uint32 createdItemId = 0;
        uint32 createdItemSellPrice = 0;
        uint32 createdItemClass = 0;
        uint32 reqSkillRank = 0;
        uint32 skillTier = 0;
        float recipeMultiplier = 1.0f;
        uint32 reagentSellTotal = 0;
        bool isEnchantSpell = false;
    };

    inline bool ParseU32(std::string const& text, uint32& out, uint32 maxValue = MAX_NUMERIC_VALUE)
    {
        if (text.empty() || text.size() > 10)
            return false;
        uint64 value = 0;
        for (char ch : text)
        {
            if (ch < '0' || ch > '9')
                return false;
            value = value * 10 + uint32(ch - '0');
            if (value > maxValue)
                return false;
        }
        out = uint32(value);
        return true;
    }

    inline std::vector<std::string> SplitBounded(std::string const& text, char delim, uint32 maxFields)
    {
        std::vector<std::string> parts;
        std::string current;
        for (char ch : text)
        {
            if (parts.size() >= maxFields)
                break;
            if (ch == delim)
            {
                parts.push_back(current);
                current.clear();
            }
            else
                current.push_back(ch);
        }
        if (parts.size() < maxFields)
            parts.push_back(current);
        return parts;
    }

    inline std::vector<FeeRange> ParseFeeRanges(std::string const& config)
    {
        std::vector<FeeRange> ranges;
        for (std::string const& token : SplitBounded(config, ';', 32))
        {
            if (token.empty())
                continue;
            auto colon = token.find(':');
            if (colon == std::string::npos)
                continue;
            auto dash = token.find('-');
            if (dash == std::string::npos || dash > colon)
                continue;
            FeeRange range;
            if (!ParseU32(token.substr(0, dash), range.minIlvl))
                continue;
            if (!ParseU32(token.substr(dash + 1, colon - dash - 1), range.maxIlvl))
                continue;
            if (!ParseU32(token.substr(colon + 1), range.copper))
                continue;
            if (range.minIlvl > range.maxIlvl)
                continue;
            ranges.push_back(range);
        }
        return ranges;
    }

    inline uint32 FeeForItemLevel(std::vector<FeeRange> const& ranges, uint32 itemLevel, uint32 defaultFee)
    {
        for (FeeRange const& range : ranges)
        {
            if (itemLevel >= range.minIlvl && itemLevel <= range.maxIlvl)
                return range.copper;
        }
        return defaultFee;
    }

    struct RecipeOverride
    {
        uint32 spellId = 0;
        uint32 professionId = 0;
        float multiplier = 1.0f;
    };

    inline std::vector<RecipeOverride> ParseRecipeOverrides(std::string const& config)
    {
        std::vector<RecipeOverride> overrides;
        for (std::string const& token : SplitBounded(config, ';', 256))
        {
            if (token.empty())
                continue;
            auto parts = SplitBounded(token, ',', 3);
            if (parts.size() < 3)
                continue;
            RecipeOverride ov;
            if (!ParseU32(parts[0], ov.spellId) || !ParseU32(parts[1], ov.professionId))
                continue;
            try
            {
                ov.multiplier = std::stof(parts[2]);
            }
            catch (...)
            {
                continue;
            }
            if (!std::isfinite(ov.multiplier))
                continue;
            overrides.push_back(ov);
        }
        return overrides;
    }

    inline std::vector<uint32> ParseIdList(std::string const& config)
    {
        std::vector<uint32> ids;
        for (std::string const& token : SplitBounded(config, ';', 512))
        {
            if (token.empty())
                continue;
            uint32 id = 0;
            if (!ParseU32(token, id))
                continue;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    inline bool IdListContains(std::vector<uint32> const& ids, uint32 id)
    {
        return std::binary_search(ids.begin(), ids.end(), id);
    }

    inline bool RecipeAllowed(uint32 spellId, std::vector<uint32> const& allow, std::vector<uint32> const& deny)
    {
        if (IdListContains(deny, spellId))
            return false;
        if (!allow.empty() && !IdListContains(allow, spellId))
            return false;
        return true;
    }

    inline uint32 CalculateRecipeFee(RecipeFeeInput const& input, FeeConfig const& config)
    {
        float tierMult = 1.0f;
        if (input.skillTier >= TIER_APPRENTICE && input.skillTier <= TIER_ARTISAN)
            tierMult = config.tierMultiplier[input.skillTier];
        if (tierMult < 0.0f)
            tierMult = 0.0f;

        float recipeMult = input.recipeMultiplier;
        if (!std::isfinite(recipeMult) || recipeMult <= 0.0f)
            return 0;
        if (!std::isfinite(tierMult))
            return 0;

        double raw = 0.0;
        if (input.professionId == PROF_ENCHANTING && (input.createdItemId == 0 || input.isEnchantSpell))
            raw = double(input.reqSkillRank) * double(config.enchantFeePerSkillPoint) * double(recipeMult) * double(tierMult);
        else if (input.createdItemSellPrice > 0)
            raw = double(input.createdItemSellPrice) * double(config.defaultFeePercent) * double(recipeMult) * double(tierMult);
        else
            raw = double(input.reagentSellTotal) * double(config.defaultFeePercent) * double(recipeMult) * double(tierMult);

        // A malformed config multiplier must never result in a NaN-to-integer
        // conversion.  Preserve the minimum-fee behavior for that case.
        if (std::isnan(raw))
            return config.minFeeCopper;
        if (raw < 0.0)
            raw = 0.0;
        if (raw > double(std::numeric_limits<uint32>::max()))
            return std::numeric_limits<uint32>::max();

        uint32 fee = uint32(raw);
        if (fee == 0 && config.minFeeCopper > 0)
            fee = config.minFeeCopper;
        return fee;
    }

    inline bool TotalReagentCount(uint32 perCraft, uint32 quantity, uint32& out)
    {
        return CheckedMulU32(perCraft, quantity, out);
    }

    inline bool TotalOutputCount(uint32 perCraft, uint32 quantity, uint32& out)
    {
        return CheckedMulU32(perCraft, quantity, out);
    }

    inline bool TotalFee(uint32 perCraft, uint32 quantity, uint32& out)
    {
        return CheckedMulU32(perCraft, quantity, out);
    }

    inline std::string EscapeField(std::string const& text)
    {
        std::string out;
        out.reserve(text.size());
        for (char ch : text)
        {
            if (ch == '\\' || ch == '|' || ch == ';' || ch == ',' || ch == '\t' || ch == '\n' || ch == '\r')
            {
                out.push_back('\\');
                if (ch == '\n')
                    out.push_back('n');
                else if (ch == '\r')
                    out.push_back('r');
                else if (ch == '\t')
                    out.push_back('t');
                else
                    out.push_back(ch);
            }
            else
                out.push_back(ch);
        }
        return out;
    }

    inline std::string UnescapeField(std::string const& text)
    {
        std::string out;
        out.reserve(text.size());
        bool escaped = false;
        for (char ch : text)
        {
            if (escaped)
            {
                if (ch == 'n')
                    out.push_back('\n');
                else if (ch == 'r')
                    out.push_back('\r');
                else if (ch == 't')
                    out.push_back('\t');
                else
                    out.push_back(ch);
                escaped = false;
            }
            else if (ch == '\\')
                escaped = true;
            else
                out.push_back(ch);
        }
        return out;
    }

    inline std::vector<std::string> SplitUnescaped(std::string const& text, char delim)
    {
        std::vector<std::string> parts;
        std::string current;
        bool escaped = false;
        for (char ch : text)
        {
            if (escaped)
            {
                current.push_back('\\');
                current.push_back(ch);
                escaped = false;
            }
            else if (ch == '\\')
                escaped = true;
            else if (ch == delim)
            {
                parts.push_back(current);
                current.clear();
            }
            else
                current.push_back(ch);
        }
        if (escaped)
            current.push_back('\\');
        parts.push_back(current);
        return parts;
    }

    inline std::string JoinRecords(std::vector<std::string> const& records)
    {
        std::string out;
        for (size_t i = 0; i < records.size(); ++i)
        {
            if (i)
                out.push_back('|');
            out += records[i];
        }
        return out;
    }

    struct ChunkResult
    {
        std::vector<std::string> chunks;
        bool overflow = false;
        bool recordTooLarge = false;
    };

    inline ChunkResult ChunkRecords(std::vector<std::string> const& records, uint32 chunkSize = MAX_CHUNK_PAYLOAD, uint32 maxChunks = MAX_RESPONSE_CHUNKS)
    {
        ChunkResult result;
        if (chunkSize == 0 || maxChunks == 0)
        {
            result.overflow = true;
            return result;
        }
        if (records.empty())
        {
            result.chunks.emplace_back();
            return result;
        }

        auto flush = [&](std::string& current) -> bool
        {
            if (result.chunks.size() >= maxChunks)
            {
                result.overflow = true;
                result.chunks.clear();
                current.clear();
                return false;
            }
            result.chunks.push_back(current);
            current.clear();
            return true;
        };

        std::string current;
        bool firstRecord = true;
        for (std::string const& rec : records)
        {
            // A record may span chunks.  Keep the complete escaped byte stream
            // intact: the client concatenates chunks before splitting records,
            // so splitting in the middle of an escape sequence is safe.
            uint64 const recordChunks = uint64(rec.size()) / uint64(chunkSize) +
                (uint64(rec.size()) % uint64(chunkSize) != 0 ? 1 : 0);
            if (recordChunks > maxChunks)
            {
                result.recordTooLarge = true;
                result.chunks.clear();
                return result;
            }
            std::string prefix = firstRecord ? std::string() : std::string(1, '|');
            firstRecord = false;
            size_t offset = 0;
            while (offset < prefix.size() ||
                   (offset >= prefix.size() && offset - prefix.size() < rec.size()))
            {
                if (current.size() == chunkSize && !flush(current))
                    return result;

                size_t const available = size_t(chunkSize) - current.size();
                size_t const prefixRemaining = offset < prefix.size() ? prefix.size() - offset : 0;
                size_t const recOffset = offset > prefix.size() ? offset - prefix.size() : 0;
                size_t const recRemaining = recOffset < rec.size() ? rec.size() - recOffset : 0;
                size_t const prefixCopyAvailable = std::min(available, prefixRemaining);
                size_t const copyCount = prefixCopyAvailable +
                    std::min(available - prefixCopyAvailable, recRemaining);
                if (copyCount == 0)
                    continue;
                size_t const prefixCopy = std::min(copyCount, prefixRemaining);
                if (prefixCopy != 0)
                    current.append(prefix, offset, prefixCopy);
                if (prefixCopy < copyCount)
                    current.append(rec, recOffset, copyCount - prefixCopy);
                offset += copyCount;
            }
        }
        if (!current.empty() && !flush(current))
            return result;
        if (result.chunks.empty())
            result.chunks.emplace_back();
        return result;
    }

    inline ChunkResult ChunkPayload(std::string const& payload, uint32 chunkSize = MAX_CHUNK_PAYLOAD, uint32 maxChunks = MAX_RESPONSE_CHUNKS)
    {
        return ChunkRecords(SplitUnescaped(payload, '|'), chunkSize, maxChunks);
    }

    inline std::string AssembleChunks(std::vector<std::string> const& chunks)
    {
        std::string out;
        for (std::string const& chunk : chunks)
            out += chunk;
        return out;
    }

    struct PagedRecords
    {
        std::vector<std::string> records;
        uint32 page = 0;
        uint32 totalPages = 1;
        bool pageOutOfRange = false;
        bool recordTooLarge = false;
    };

    inline uint32 ChunksNeededForRecords(std::vector<std::string> const& records, uint32 chunkSize)
    {
        if (records.empty())
            return 1;
        if (chunkSize == 0)
            return std::numeric_limits<uint32>::max();

        uint64 totalBytes = 0;
        for (size_t i = 0; i < records.size(); ++i)
        {
            uint64 const separator = i == 0 ? 0u : 1u;
            if (uint64(records[i].size()) > std::numeric_limits<uint64>::max() - separator)
                return std::numeric_limits<uint32>::max();
            uint64 const extra = uint64(records[i].size()) + separator;
            if (totalBytes > std::numeric_limits<uint64>::max() - extra)
                return std::numeric_limits<uint32>::max();
            totalBytes += extra;
        }
        uint64 const chunks = totalBytes / uint64(chunkSize) + (totalBytes % uint64(chunkSize) != 0 ? 1 : 0);
        if (chunks > std::numeric_limits<uint32>::max())
            return std::numeric_limits<uint32>::max();
        return uint32(chunks == 0 ? 1 : chunks);
    }

    inline PagedRecords PaginateRecords(std::vector<std::string> const& records, uint32 page, uint32 chunkSize = MAX_CHUNK_PAYLOAD, uint32 maxChunks = MAX_RESPONSE_CHUNKS)
    {
        PagedRecords result;
        result.page = page;
        if (records.empty())
            return result;
        if (chunkSize == 0 || maxChunks == 0)
        {
            result.recordTooLarge = true;
            return result;
        }

        std::vector<std::vector<std::string>> pages;
        std::vector<std::string> current;
        uint64 currentBytes = 0;
        for (std::string const& rec : records)
        {
            // Pages are made only at record boundaries.  A single long record
            // is valid when its split chunks fit the page's chunk budget.
            uint64 const recordBytes = uint64(rec.size());
            uint64 const recordChunks = recordBytes / uint64(chunkSize) +
                (recordBytes % uint64(chunkSize) != 0 ? 1 : 0);
            if (recordChunks > maxChunks)
            {
                result.recordTooLarge = true;
                return result;
            }
            uint64 separator = current.empty() ? 0u : 1u;
            bool fits = recordBytes <= std::numeric_limits<uint64>::max() - separator;
            uint64 extra = fits ? recordBytes + separator : 0;
            if (fits)
            {
                fits = currentBytes <= std::numeric_limits<uint64>::max() - extra;
            }
            if (fits)
            {
                uint64 const candidateBytes = currentBytes + extra;
                uint64 const candidateChunks = candidateBytes / uint64(chunkSize) +
                    (candidateBytes % uint64(chunkSize) != 0 ? 1 : 0);
                fits = candidateChunks <= maxChunks;
            }
            if (!fits && !current.empty())
            {
                pages.push_back(std::move(current));
                current.clear();
                currentBytes = 0;
                separator = 0;
                extra = recordBytes;
                fits = recordChunks <= maxChunks;
            }
            if (!fits)
            {
                result.recordTooLarge = true;
                return result;
            }
            current.push_back(rec);
            currentBytes += extra;
        }
        if (!current.empty())
            pages.push_back(std::move(current));

        result.totalPages = pages.empty() ? 1 : uint32(pages.size());
        if (page >= result.totalPages)
        {
            result.pageOutOfRange = true;
            return result;
        }
        result.records = pages[page];
        return result;
    }

    struct HandInRecord
    {
        uint32 itemGuid = 0;
        uint32 itemId = 0;
        std::string name;
        uint32 taughtSpell = 0;
        uint32 createdItemId = 0;
        bool valid = false;
    };

    inline HandInRecord ParseHandInRecord(std::string const& entry)
    {
        HandInRecord rec;
        auto fields = SplitUnescaped(entry, ',');
        if (fields.size() < 3)
            return rec;
        if (!ParseU32(UnescapeField(fields[0]), rec.itemGuid, std::numeric_limits<uint32>::max()) || rec.itemGuid == 0)
            return rec;
        if (!ParseU32(UnescapeField(fields[1]), rec.itemId))
            return rec;
        rec.name = UnescapeField(fields[2]);
        if (fields.size() >= 4)
            ParseU32(UnescapeField(fields[3]), rec.taughtSpell);
        if (fields.size() >= 5)
            ParseU32(UnescapeField(fields[4]), rec.createdItemId);
        rec.valid = true;
        return rec;
    }

    inline std::string ToLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    }

    struct ProtocolRequest
    {
        uint32 version = 0;
        uint32 requestId = 0;
        std::string opcode;
        std::vector<std::string> fields;
        bool valid = false;
        std::string error;
    };

    inline ProtocolRequest ParseProtocolPayload(std::string const& payload)
    {
        ProtocolRequest req;
        if (payload.size() > MAX_MESSAGE_SIZE)
        {
            req.error = "message too large";
            return req;
        }

        auto parts = SplitBounded(payload, '\t', MAX_FIELD_COUNT);
        if (parts.size() < 3)
        {
            req.error = "missing fields";
            return req;
        }
        if (!ParseU32(parts[0], req.version) || req.version == 0)
        {
            req.error = "bad version";
            return req;
        }
        if (!ParseU32(parts[1], req.requestId, std::numeric_limits<uint32>::max()))
        {
            req.error = "bad request id";
            return req;
        }
        req.opcode = parts[2];
        if (req.opcode.empty() || req.opcode.size() > 32)
        {
            req.error = "bad opcode";
            return req;
        }
        for (char ch : req.opcode)
        {
            if (!((ch >= 'A' && ch <= 'Z') || ch == '_'))
            {
                req.error = "bad opcode";
                return req;
            }
        }
        for (size_t i = 3; i < parts.size(); ++i)
            req.fields.push_back(parts[i]);
        req.valid = true;
        return req;
    }

    inline std::string FormatProtocolHeader(uint32 version, uint32 requestId, uint32 part, uint32 total, std::string const& opcode, uint32 page = 0, uint32 totalPages = 1)
    {
        return std::to_string(version) + "\t" + std::to_string(requestId) + "\t" +
               std::to_string(part) + "\t" + std::to_string(total) + "\t" + opcode + "\t" +
               std::to_string(page) + "\t" + std::to_string(totalPages == 0 ? 1 : totalPages);
    }

    inline std::string FormatMoney(uint32 copper)
    {
        uint32 gold = copper / GOLD;
        uint32 silver = (copper % GOLD) / SILVER;
        uint32 c = copper % SILVER;
        std::string out;
        if (gold > 0)
            out += std::to_string(gold) + "g ";
        if (silver > 0 || gold > 0)
            out += std::to_string(silver) + "s ";
        out += std::to_string(c) + "c";
        return out;
    }

    inline bool ReplayRejected(uint32 lastRequestId, uint32 requestId)
    {
        return requestId == 0 || requestId <= lastRequestId;
    }
}

#endif
