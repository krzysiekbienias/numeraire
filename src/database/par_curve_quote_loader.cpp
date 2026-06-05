#include <SQLiteCpp/SQLiteCpp.h>

#include <numeraire/database/par_curve_quote_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>
#include <string>
#include <vector>

namespace numeraire::database {

quant::CurvePillarKind ParCurveInstrumentToKind(const std::string& instrument_type) {
    const std::string key = utils::ToLowerAscii(utils::TrimCopy(instrument_type));
    if (key == "deposit") {
        return quant::CurvePillarKind::kDeposit;
    }
    if (key == "futures") {
        return quant::CurvePillarKind::kFutures;
    }
    if (key == "swap") {
        return quant::CurvePillarKind::kSwap;
    }
    throw ValidationError("par curve pillar: unsupported instrument_type: " + instrument_type);
}

std::vector<quant::CurvePillarQuote> ToCurvePillarQuotes(const std::vector<ParCurvePillarInput>& pillars) {
    std::vector<quant::CurvePillarQuote> out;
    out.reserve(pillars.size());
    for (const ParCurvePillarInput& pillar : pillars) {
        if (pillar.tenor_days <= 0) {
            throw ValidationError("par curve pillar " + pillar.tenor + ": missing tenor_days");
        }
        out.push_back(quant::CurvePillarQuote{
                .tenor = pillar.tenor,
                .tenor_days = pillar.tenor_days,
                .kind = ParCurveInstrumentToKind(pillar.instrument_type),
                .quoted_rate = pillar.quoted_rate,
        });
    }
    return out;
}

std::optional<ParCurveEodHeader> LoadParCurveEodHeader(const std::string& database_file_path,
                                                       const std::string_view curve_id,
                                                       const std::string_view as_of_iso_yyyy_mm_dd) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(db,
                             "SELECT curve_id, as_of, currency, day_count, session_calendar "
                             "FROM par_curve_eod WHERE curve_id = ? AND as_of = ?");
        st.bind(1, std::string(curve_id));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        if (!st.executeStep()) {
            return std::nullopt;
        }
        return ParCurveEodHeader{
                .curve_id = st.getColumn(0).getString(),
                .as_of = st.getColumn(1).getString(),
                .currency = st.getColumn(2).getString(),
                .day_count = st.getColumn(3).getString(),
                .session_calendar = st.getColumn(4).getString(),
        };
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadParCurveEodHeader: "} + e.what());
    }
}

std::vector<ParCurvePillarInput> LoadParCurvePillarInputs(const std::string& database_file_path,
                                                          const std::string_view curve_id,
                                                          const std::string_view as_of_iso_yyyy_mm_dd) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(db,
                             "SELECT tenor, tenor_days, instrument_type, quoted_rate "
                             "FROM par_curve_point_eod WHERE curve_id = ? AND as_of = ? "
                             "ORDER BY tenor_days, tenor");
        st.bind(1, std::string(curve_id));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));

        std::vector<ParCurvePillarInput> out;
        while (st.executeStep()) {
            ParCurvePillarInput row{};
            row.tenor = st.getColumn(0).getString();
            if (!st.getColumn(1).isNull()) {
                row.tenor_days = st.getColumn(1).getInt();
            }
            row.instrument_type = st.getColumn(2).getString();
            row.quoted_rate = st.getColumn(3).getDouble();
            out.push_back(std::move(row));
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadParCurvePillarInputs: "} + e.what());
    }
}

}  // namespace numeraire::database
