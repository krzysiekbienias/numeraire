#include <numeraire/products/product_factory.hpp>

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

enum class EquityCatalogInstrumentKind : std::uint8_t {
    kVanilla,
    kAssetOrNothing,
};

[[nodiscard]] std::string NormalizeInstrumentTypeKey(std::string t) {
    t = numeraire::utils::ToLowerAscii(numeraire::utils::TrimCopy(t));
    t.erase(std::remove_if(t.begin(), t.end(),
                           [](const unsigned char c) { return c == '_' || std::isspace(c) != 0; }),
            t.end());
    return t;
}


[[nodiscard]] numeraire::OptionType ParseOptionSide(const std::optional<std::string>& side) {
    if (!side.has_value()) {
        throw numeraire::ValidationError("equity catalog product requires option_side (CALL/PUT)");
    }
    const std::string k = numeraire::utils::ToLowerAscii(numeraire::utils::TrimCopy(*side));
    if (k == "call") {
        return numeraire::OptionType::kCall;
    }
    if (k == "put") {
        return numeraire::OptionType::kPut;
    }
    throw numeraire::ValidationError("option_side must be CALL or PUT, got: " + *side);
}

[[nodiscard]] EquityCatalogInstrumentKind KindFromNormalizedInstrumentTypeKey(const std::string& key) {
    if (key.empty() || key == "vanilla" || key == "vanillaoption" ||
        key == "plainvanillaeuropeanoption") {
        return EquityCatalogInstrumentKind::kVanilla;
    }
    if (key == "assetornothingoption" || key == "assetornothing") {
        return EquityCatalogInstrumentKind::kAssetOrNothing;
    }
    std::ostringstream oss;
    oss << "unsupported instrument_type: \"" << key << "\"";
    throw numeraire::ValidationError(oss.str());
}

[[nodiscard]] EquityCatalogInstrumentKind ParseEquityInstrumentKindFromAttributesJson(
        const std::string& attributes_json) {
    const std::string trimmed = numeraire::utils::TrimCopy(attributes_json);
    if (trimmed.empty() || trimmed == "{}") {
        return EquityCatalogInstrumentKind::kVanilla;
    }

    const auto j = nlohmann::json::parse(trimmed, nullptr, false);
    if (j.is_discarded()) {
        throw numeraire::ValidationError("attributes_json is not valid JSON");
    }
    if (!j.is_object()) {
        throw numeraire::ValidationError("attributes_json must be a JSON object");
    }

    const auto it = j.find("instrument_type");
    if (it == j.end() || it->is_null()) {
        return EquityCatalogInstrumentKind::kVanilla;
    }
    if (!it->is_string()) {
        throw numeraire::ValidationError("instrument_type must be a string");
    }

    const std::string key = NormalizeInstrumentTypeKey(it->get<std::string>());
    try {
        return KindFromNormalizedInstrumentTypeKey(key);
    } catch (const numeraire::ValidationError&) {
        std::ostringstream oss;
        oss << "unsupported instrument_type for equity catalog: \"" << it->get<std::string>() << "\"";
        throw numeraire::ValidationError(oss.str());
    }
}

[[nodiscard]] EquityCatalogInstrumentKind ResolveEquityInstrumentKind(
        const std::optional<std::string>& catalog_type, const std::string& attributes_json) {
    if (catalog_type.has_value()) {
        const std::string t = numeraire::utils::TrimCopy(*catalog_type);
        if (!t.empty()) {
            return KindFromNormalizedInstrumentTypeKey(NormalizeInstrumentTypeKey(t));
        }
    }
    return ParseEquityInstrumentKindFromAttributesJson(attributes_json);
}

[[nodiscard]] numeraire::ExerciseStyle ResolveCatalogExerciseStyle(
        const std::optional<std::string>& catalog_exercise) {
    if (!catalog_exercise.has_value()) {
        return numeraire::ExerciseStyle::kEuropean;
    }
    const std::string k = numeraire::utils::ToLowerAscii(numeraire::utils::TrimCopy(*catalog_exercise));
    if (k.empty() || k == "european") {
        return numeraire::ExerciseStyle::kEuropean;
    }
    if (k == "american") {
        throw numeraire::ValidationError("ProductFactory: american exercise_style is not implemented yet");
    }
    throw numeraire::ValidationError("ProductFactory: unsupported exercise_style: " + *catalog_exercise);
}

void EnsureMatchingProductIds(const std::string& a, const std::string& b) {
    if (a != b) {
        throw numeraire::ValidationError("product_id mismatch between ProductDto and ProductEquityDto");
    }
}

void EnsureEquityKind(const std::string& asset_kind) {
    if (numeraire::utils::ToLowerAscii(numeraire::utils::TrimCopy(asset_kind)) != "equity") {
        throw numeraire::ValidationError("ProductFactory::MakeFromEquityCatalog expects asset_kind EQUITY");
    }
}

}  // namespace

namespace numeraire::products {

std::unique_ptr<core::IProduct> ProductFactory::MakeFromEquityCatalog(
        const database::ProductDto& product,
        const database::ProductEquityDto& equity,
        const database::TradeHeaderDto* trade_header) {
    EnsureMatchingProductIds(product.product_id, equity.product_id);
    EnsureEquityKind(equity.asset_kind);
    const EquityCatalogInstrumentKind kind =
            ResolveEquityInstrumentKind(product.catalog_instrument_type, product.attributes_json);
    const ExerciseStyle exercise = ResolveCatalogExerciseStyle(product.catalog_exercise_style);

    if (!equity.expiry_date.has_value() || numeraire::utils::TrimCopy(*equity.expiry_date).empty()) {
        throw ValidationError("equity catalog requires expiry_date for this instrument");
    }
    const schedule::Date expiry = schedule::ParseIsoDate(*equity.expiry_date);
    const schedule::Date trade_date =
            trade_header != nullptr && !numeraire::utils::TrimCopy(trade_header->trade_date).empty()
                    ? schedule::ParseIsoDate(trade_header->trade_date)
                    : expiry;

    const OptionType opt = ParseOptionSide(product.option_side);
    if (!product.strike.has_value()) {
        throw ValidationError("equity option product requires strike (barrier level K)");
    }

    switch (kind) {
        case EquityCatalogInstrumentKind::kVanilla:
            return std::make_unique<VanillaEquityOptionProduct>(
                    equity.underlying_id, opt, exercise, *product.strike, trade_date, expiry);
        case EquityCatalogInstrumentKind::kAssetOrNothing:
            return std::make_unique<EquityAssetOrNothingProduct>(
                    equity.underlying_id, opt, exercise, *product.strike, trade_date, expiry);
    }
    throw ValidationError("internal: unhandled equity instrument kind");
}

}  // namespace numeraire::products
