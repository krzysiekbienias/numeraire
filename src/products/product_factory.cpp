#include <numeraire/products/product_factory.hpp>

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/utils/exception.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

[[nodiscard]] std::string TrimCopy(std::string s) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

[[nodiscard]] std::string ToLowerAscii(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

[[nodiscard]] numeraire::schedule::Date ParseIsoDate(const std::string& raw) {
    const std::string s = TrimCopy(raw);
    if (s.size() != 10U || s[4] != '-' || s[7] != '-') {
        throw numeraire::ValidationError("expected ISO date YYYY-MM-DD, got: " + raw);
    }
    int year{};
    int month{};
    int day{};
    const char* const p = s.c_str();
    if (std::sscanf(p, "%d-%d-%d", &year, &month, &day) != 3) {
        throw numeraire::ValidationError("invalid ISO date: " + raw);
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        throw numeraire::ValidationError("date out of range: " + raw);
    }
    return numeraire::schedule::Date{.year = year, .month = month, .day = day};
}

[[nodiscard]] numeraire::OptionType ParseOptionSide(const std::optional<std::string>& side) {
    if (!side.has_value()) {
        throw numeraire::ValidationError("vanilla product requires option_side (CALL/PUT)");
    }
    const std::string k = ToLowerAscii(TrimCopy(*side));
    if (k == "call") {
        return numeraire::OptionType::kCall;
    }
    if (k == "put") {
        return numeraire::OptionType::kPut;
    }
    throw numeraire::ValidationError("option_side must be CALL or PUT, got: " + *side);
}

void EnsureVanillaAttributesJson(const std::string& attributes_json) {
    const std::string trimmed = TrimCopy(attributes_json);
    if (trimmed.empty() || trimmed == "{}") {
        return;
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
        return;
    }
    if (!it->is_string()) {
        throw numeraire::ValidationError("instrument_type must be a string");
    }

    const std::string t = ToLowerAscii(TrimCopy(it->get<std::string>()));
    if (t.empty() || t == "vanilla" || t == "vanillaoption") {
        return;
    }

    std::ostringstream oss;
    oss << "unsupported instrument_type for equity catalog: \"" << it->get<std::string>() << "\"";
    throw numeraire::ValidationError(oss.str());
}

void EnsureMatchingProductIds(const std::string& a, const std::string& b) {
    if (a != b) {
        throw numeraire::ValidationError("product_id mismatch between ProductDto and ProductEquityDto");
    }
}

void EnsureEquityKind(const std::string& asset_kind) {
    if (ToLowerAscii(TrimCopy(asset_kind)) != "equity") {
        throw numeraire::ValidationError("ProductFactory::MakeFromEquityCatalog expects asset_kind EQUITY");
    }
}

}  // namespace

namespace numeraire::products {

std::unique_ptr<core::IProduct> ProductFactory::MakeFromEquityCatalog(const database::ProductDto& product,
                                                                      const database::ProductEquityDto& equity,
                                                                      const database::TradeDto* trade) {
    EnsureMatchingProductIds(product.product_id, equity.product_id);
    EnsureEquityKind(equity.asset_kind);
    EnsureVanillaAttributesJson(product.attributes_json);

    const schedule::Date expiry = ParseIsoDate(equity.expiry_date);
    const schedule::Date trade_date =
            trade != nullptr ? ParseIsoDate(trade->trade_date) : expiry;

    const OptionType opt = ParseOptionSide(product.option_side);
    if (!product.strike.has_value()) {
        throw ValidationError("vanilla product requires strike");
    }

    return std::make_unique<VanillaEquityOptionProduct>(equity.underlying_id, opt, ExerciseStyle::kEuropean,
                                                        *product.strike, trade_date, expiry);
}

}  // namespace numeraire::products
