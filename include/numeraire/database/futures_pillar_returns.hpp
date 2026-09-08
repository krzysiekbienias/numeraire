#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {

/// Factor id for a constant-maturity futures pillar, e.g. ("CL", 1) -> "CL_M1".
/// Calibration and simulation must agree on this spelling, so both go through here.
[[nodiscard]] std::string FuturesPillarFactorId(std::string_view product_code, int pillar);

/// One single-session log-return of a constant-maturity pillar.
struct PillarReturn {
    /// Session the return lands on; the move spans the previous session to this one.
    std::string as_of;
    double log_return{0.0};
};

/// One constant-maturity pillar of a futures curve (M1 = front month, M2 = next, ...).
///
/// Returns rather than a price series, because a pillar has no continuous price:
/// it hops between contracts on every roll. Each entry is a genuine single-contract,
/// single-session move, so vol and correlation can be estimated directly. The series
/// may have holes where the vendor did not quote the occupying contract on both
/// sessions; consumers must align on `as_of` rather than assume equal lengths.
struct FuturesPillarSeries {
    std::string factor_id;
    std::string product_code;
    /// 1-based: 1 = nearest contract that has not settled yet.
    int pillar{0};
    std::vector<PillarReturn> returns;
    /// Real settle of the contract on this pillar at `last_date` — the price anchor
    /// a simulation should start the pillar from.
    double level_on_last_date{0.0};
    std::string ticker_on_last_date;
    std::string last_date;
    /// Sessions on which the pillar existed, whether or not a return was computable.
    int num_sessions_present{0};
};

/// Build roll-adjusted constant-maturity pillars for `product_code` (e.g. "CL", "NG")
/// from `futures_daily_eod` joined to `futures_contract` settlement dates.
///
/// On each session the unsettled contracts are ranked by settlement date and the
/// first `num_pillars` occupy M1..Mn. A session's return is measured on the contract
/// occupying the pillar *that* session, priced on the immediately preceding session.
/// Taking the previous session's occupant instead would break on roll days, when it
/// has already settled and is no longer quoted.
///
/// Sessions whose occupant was not quoted the session before simply produce no
/// return, rather than differencing two contracts or stretching a multi-session move
/// into a daily one.
///
/// Pillars with no computable return are omitted from the result.
[[nodiscard]] std::vector<FuturesPillarSeries> LoadFuturesPillarReturns(
        const std::string& database_file_path,
        std::string_view product_code,
        std::string_view from_iso_yyyy_mm_dd,
        std::string_view to_iso_yyyy_mm_dd,
        int num_pillars = 6);

/// Which dated contract sits on a constant-maturity pillar on one session.
struct FuturesPillarOccupant {
    /// 1-based: 1 = nearest contract that has not settled yet.
    int pillar{0};
    std::string factor_id;
    std::string ticker;
    std::string settlement_date;
};

/// The pillar occupants of `product_code` on `as_of_iso`, ranked by settlement date.
///
/// This is what turns a pillar factor into a dated tenor: `settlement_date` minus
/// `as_of_iso` is the maturity a constant-maturity pillar stands for, which is how a
/// booked contract is located on the calibrated strip. Ranking matches
/// `LoadFuturesPillarReturns`, so pillar k here is the factor calibrated as `Mk`.
/// Requires a quoted session on exactly `as_of_iso`; for pricing that should follow
/// the last available curve, use `LoadLatestFuturesPillarCurve`.
[[nodiscard]] std::vector<FuturesPillarOccupant> LoadFuturesPillarCurve(
        const std::string& database_file_path,
        std::string_view product_code,
        std::string_view as_of_iso,
        int num_pillars = 6);

/// Occupants of a constant-maturity strip as of one quoted session.
struct FuturesPillarCurve {
    /// Session the contracts were ranked on. May lag the date the caller asked for.
    std::string as_of;
    std::vector<FuturesPillarOccupant> occupants;
};

/// Same ranking as `LoadFuturesPillarCurve`, on the latest quoted session at or
/// before `on_or_before_as_of_iso`. Empty `occupants` when the product has no curve
/// on or before that date. Calibration snapshots stay exact-date; path pricing
/// uses this so a book calibrated on one session still prices on later days.
[[nodiscard]] FuturesPillarCurve LoadLatestFuturesPillarCurve(
        const std::string& database_file_path,
        std::string_view product_code,
        std::string_view on_or_before_as_of_iso,
        int num_pillars = 6);

/// One quoted contract on the settlement curve for a single session.
struct FuturesCurveQuote {
    std::string ticker;
    std::string settlement_date;
    double price{0.0};
};

/// Every unsettled, quoted contract of `product_code` on `as_of_iso`, by settlement date.
///
/// Uncapped, unlike the pillar view. A curve fit wants the whole strip precisely because
/// the deferred years are where a seasonal shape shows itself repeating — that repetition
/// is observable in one session, without waiting years for the history to accumulate.
[[nodiscard]] std::vector<FuturesCurveQuote> LoadFuturesCurveSnapshot(
        const std::string& database_file_path,
        std::string_view product_code,
        std::string_view as_of_iso);

}  // namespace numeraire::database
