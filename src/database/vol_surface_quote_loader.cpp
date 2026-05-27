#include <numeraire/database/vol_surface_quote_loader.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <string>
#include <vector>

namespace numeraire::database {
namespace {

[[nodiscard]] OptionType ParseOptionType(const std::string& contract_type) {
    const std::string key = utils::ToLowerAscii(utils::TrimCopy(contract_type));
    if (key == "call") {
        return OptionType::kCall;
    }
    if (key == "put") {
        return OptionType::kPut;
    }
    throw ValidationError("vol surface quote: unsupported contract_type: " + contract_type);
}

}  // namespace

std::vector<VolSurfaceOptionQuoteInput> LoadVolSurfaceOptionQuotes(const std::string& database_file_path,
                                                                   const std::string_view underlying_ticker,
                                                                   const std::string_view as_of_iso_yyyy_mm_dd,
                                                                   const int adjusted) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(
                db,
                "SELECT p.option_ticker, p.close, c.strike_price, c.expiration_date, c.contract_type, "
                "c.exercise_style "
                "FROM option_daily_price_eod p "
                "INNER JOIN option_contract c ON c.option_ticker = p.option_ticker "
                "INNER JOIN ("
                "  SELECT option_ticker, MAX(listing_as_of) AS listing_as_of "
                "  FROM option_contract "
                "  WHERE underlying_ticker = ? "
                "  GROUP BY option_ticker"
                ") latest ON latest.option_ticker = c.option_ticker AND latest.listing_as_of = c.listing_as_of "
                "WHERE p.as_of = ? AND c.underlying_ticker = ? AND p.timespan = '1d' AND p.adjusted = ? "
                "ORDER BY c.expiration_date, c.strike_price, p.option_ticker");
        st.bind(1, std::string(underlying_ticker));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        st.bind(3, std::string(underlying_ticker));
        st.bind(4, adjusted);

        std::vector<VolSurfaceOptionQuoteInput> out;
        while (st.executeStep()) {
            VolSurfaceOptionQuoteInput row{};
            row.option_ticker = st.getColumn(0).getString();
            row.close = st.getColumn(1).getDouble();
            row.strike = st.getColumn(2).getDouble();
            row.expiration_date = st.getColumn(3).getString();
            row.option_type = ParseOptionType(st.getColumn(4).getString());
            row.exercise_style = st.getColumn(5).getString();
            out.push_back(std::move(row));
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadVolSurfaceOptionQuotes: "} + e.what());
    }
}

}  // namespace numeraire::database
