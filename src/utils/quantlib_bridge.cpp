#include <numeraire/utils/quantlib_bridge.hpp>

#include <numeraire/utils/exception.hpp>

#include <ql/currencies/america.hpp>
#include <ql/currencies/asia.hpp>
#include <ql/currencies/europe.hpp>
#include <ql/currencies/oceania.hpp>
#include <ql/time/calendars/germany.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/time/calendars/unitedkingdom.hpp>
#include <ql/time/calendars/unitedstates.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/time/daycounters/actualactual.hpp>
#include <ql/time/daycounters/business252.hpp>
#include <ql/time/daycounters/thirty360.hpp>

#include <sstream>
#include <string>

namespace numeraire::utils::quantlib_bridge {
namespace {

[[noreturn]] void ThrowUnsupported(const std::string& what) {
    throw numeraire::ValidationError("quantlib_bridge: " + what);
}

}  // namespace

QuantLib::Option::Type ToQuantLib(const OptionType option_type) {
    switch (option_type) {
        case OptionType::kCall:
            return QuantLib::Option::Call;
        case OptionType::kPut:
            return QuantLib::Option::Put;
    }
    ThrowUnsupported("unsupported OptionType");
}

OptionType FromQuantLib(const QuantLib::Option::Type option_type) {
    switch (option_type) {
        case QuantLib::Option::Call:
            return OptionType::kCall;
        case QuantLib::Option::Put:
            return OptionType::kPut;
    }
    std::ostringstream oss;
    oss << "unsupported QuantLib::Option::Type value " << static_cast<int>(option_type);
    ThrowUnsupported(oss.str());
}

QuantLib::Exercise::Type ToQuantLib(const ExerciseStyle style) {
    switch (style) {
        case ExerciseStyle::kEuropean:
            return QuantLib::Exercise::European;
        case ExerciseStyle::kAmerican:
            return QuantLib::Exercise::American;
        case ExerciseStyle::kBermudan:
            return QuantLib::Exercise::Bermudan;
    }
    ThrowUnsupported("unsupported ExerciseStyle");
}

ExerciseStyle FromQuantLib(const QuantLib::Exercise::Type exercise_type) {
    switch (exercise_type) {
        case QuantLib::Exercise::European:
            return ExerciseStyle::kEuropean;
        case QuantLib::Exercise::American:
            return ExerciseStyle::kAmerican;
        case QuantLib::Exercise::Bermudan:
            return ExerciseStyle::kBermudan;
    }
    std::ostringstream oss;
    oss << "unsupported QuantLib::Exercise::Type value " << static_cast<int>(exercise_type);
    ThrowUnsupported(oss.str());
}

QuantLib::Calendar ToQuantLib(const CalendarType calendar_type) {
    switch (calendar_type) {
        case CalendarType::kUnitedKingdom:
            return QuantLib::UnitedKingdom();
        case CalendarType::kUnitedStates:
            return QuantLib::UnitedStates(QuantLib::UnitedStates::Settlement);
        case CalendarType::kTarget:
            return QuantLib::TARGET();
        case CalendarType::kGermany:
            return QuantLib::Germany(QuantLib::Germany::Settlement);
    }
    ThrowUnsupported("unsupported CalendarType");
}

CalendarType FromQuantLib(const QuantLib::Calendar& calendar) {
    const std::string name = calendar.name();
    if (name == "UK settlement") {
        return CalendarType::kUnitedKingdom;
    }
    if (name == "US settlement") {
        return CalendarType::kUnitedStates;
    }
    if (name == "TARGET") {
        return CalendarType::kTarget;
    }
    if (name == "German settlement") {
        return CalendarType::kGermany;
    }
    ThrowUnsupported("unsupported QuantLib::Calendar name '" + name + "'");
}

QuantLib::Frequency ToQuantLib(const Frequency frequency) {
    switch (frequency) {
        case Frequency::kNoFrequency:
            return QuantLib::NoFrequency;
        case Frequency::kOnce:
            return QuantLib::Once;
        case Frequency::kAnnual:
            return QuantLib::Annual;
        case Frequency::kSemiannual:
            return QuantLib::Semiannual;
        case Frequency::kEveryFourthMonth:
            return QuantLib::EveryFourthMonth;
        case Frequency::kQuarterly:
            return QuantLib::Quarterly;
        case Frequency::kBimonthly:
            return QuantLib::Bimonthly;
        case Frequency::kMonthly:
            return QuantLib::Monthly;
        case Frequency::kEveryFourthWeek:
            return QuantLib::EveryFourthWeek;
        case Frequency::kBiweekly:
            return QuantLib::Biweekly;
        case Frequency::kWeekly:
            return QuantLib::Weekly;
        case Frequency::kDaily:
            return QuantLib::Daily;
        case Frequency::kOtherFrequency:
            return QuantLib::OtherFrequency;
    }
    ThrowUnsupported("unsupported Frequency");
}

Frequency FromQuantLib(const QuantLib::Frequency frequency) {
    switch (frequency) {
        case QuantLib::NoFrequency:
            return Frequency::kNoFrequency;
        case QuantLib::Once:
            return Frequency::kOnce;
        case QuantLib::Annual:
            return Frequency::kAnnual;
        case QuantLib::Semiannual:
            return Frequency::kSemiannual;
        case QuantLib::EveryFourthMonth:
            return Frequency::kEveryFourthMonth;
        case QuantLib::Quarterly:
            return Frequency::kQuarterly;
        case QuantLib::Bimonthly:
            return Frequency::kBimonthly;
        case QuantLib::Monthly:
            return Frequency::kMonthly;
        case QuantLib::EveryFourthWeek:
            return Frequency::kEveryFourthWeek;
        case QuantLib::Biweekly:
            return Frequency::kBiweekly;
        case QuantLib::Weekly:
            return Frequency::kWeekly;
        case QuantLib::Daily:
            return Frequency::kDaily;
        case QuantLib::OtherFrequency:
            return Frequency::kOtherFrequency;
    }
    std::ostringstream oss;
    oss << "unsupported QuantLib::Frequency value " << static_cast<int>(frequency);
    ThrowUnsupported(oss.str());
}

QuantLib::DayCounter ToQuantLib(const DayCount day_count) {
    switch (day_count) {
        case DayCount::kActual360:
            return QuantLib::Actual360();
        case DayCount::kActual365Fixed:
            return QuantLib::Actual365Fixed();
        case DayCount::kThirty360:
            return QuantLib::Thirty360(QuantLib::Thirty360::USA);
        case DayCount::kActualActualIsda:
            return QuantLib::ActualActual(QuantLib::ActualActual::ISDA);
        case DayCount::kActualActualBond:
            return QuantLib::ActualActual(QuantLib::ActualActual::Bond);
        case DayCount::kBusiness252:
            return QuantLib::Business252();
    }
    ThrowUnsupported("unsupported DayCount");
}

DayCount FromQuantLib(const QuantLib::DayCounter& day_counter) {
    if (day_counter == QuantLib::Actual360()) {
        return DayCount::kActual360;
    }
    if (day_counter == QuantLib::Actual365Fixed()) {
        return DayCount::kActual365Fixed;
    }
    if (day_counter == QuantLib::Thirty360(QuantLib::Thirty360::USA)) {
        return DayCount::kThirty360;
    }
    if (day_counter == QuantLib::ActualActual(QuantLib::ActualActual::ISDA)) {
        return DayCount::kActualActualIsda;
    }
    if (day_counter == QuantLib::ActualActual(QuantLib::ActualActual::Bond)) {
        return DayCount::kActualActualBond;
    }
    if (day_counter == QuantLib::Business252()) {
        return DayCount::kBusiness252;
    }
    ThrowUnsupported("unsupported QuantLib::DayCounter '" + day_counter.name() + "'");
}

QuantLib::BusinessDayConvention ToQuantLib(const DateConvention convention) {
    switch (convention) {
        case DateConvention::kFollowing:
            return QuantLib::Following;
        case DateConvention::kModifiedFollowing:
            return QuantLib::ModifiedFollowing;
        case DateConvention::kPreceding:
            return QuantLib::Preceding;
        case DateConvention::kModifiedPreceding:
            return QuantLib::ModifiedPreceding;
        case DateConvention::kUnadjusted:
            return QuantLib::Unadjusted;
        case DateConvention::kHalfMonthModifiedFollowing:
            return QuantLib::HalfMonthModifiedFollowing;
        case DateConvention::kNearest:
            return QuantLib::Nearest;
    }
    ThrowUnsupported("unsupported DateConvention");
}

DateConvention FromQuantLib(const QuantLib::BusinessDayConvention convention) {
    switch (convention) {
        case QuantLib::Following:
            return DateConvention::kFollowing;
        case QuantLib::ModifiedFollowing:
            return DateConvention::kModifiedFollowing;
        case QuantLib::Preceding:
            return DateConvention::kPreceding;
        case QuantLib::ModifiedPreceding:
            return DateConvention::kModifiedPreceding;
        case QuantLib::Unadjusted:
            return DateConvention::kUnadjusted;
        case QuantLib::HalfMonthModifiedFollowing:
            return DateConvention::kHalfMonthModifiedFollowing;
        case QuantLib::Nearest:
            return DateConvention::kNearest;
    }
    std::ostringstream oss;
    oss << "unsupported QuantLib::BusinessDayConvention value " << static_cast<int>(convention);
    ThrowUnsupported(oss.str());
}

QuantLib::DateGeneration::Rule ToQuantLib(const DateGenerationRule rule) {
    switch (rule) {
        case DateGenerationRule::kBackward:
            return QuantLib::DateGeneration::Backward;
        case DateGenerationRule::kForward:
            return QuantLib::DateGeneration::Forward;
        case DateGenerationRule::kZero:
            return QuantLib::DateGeneration::Zero;
        case DateGenerationRule::kThirdWednesday:
            return QuantLib::DateGeneration::ThirdWednesday;
        case DateGenerationRule::kThirdWednesdayInclusive:
            return QuantLib::DateGeneration::ThirdWednesdayInclusive;
        case DateGenerationRule::kTwentieth:
            return QuantLib::DateGeneration::Twentieth;
        case DateGenerationRule::kTwentiethImm:
            return QuantLib::DateGeneration::TwentiethIMM;
        case DateGenerationRule::kOldCds:
            return QuantLib::DateGeneration::OldCDS;
        case DateGenerationRule::kCds:
            return QuantLib::DateGeneration::CDS;
        case DateGenerationRule::kCds2015:
            return QuantLib::DateGeneration::CDS2015;
    }
    ThrowUnsupported("unsupported DateGenerationRule");
}

DateGenerationRule FromQuantLib(const QuantLib::DateGeneration::Rule rule) {
    switch (rule) {
        case QuantLib::DateGeneration::Backward:
            return DateGenerationRule::kBackward;
        case QuantLib::DateGeneration::Forward:
            return DateGenerationRule::kForward;
        case QuantLib::DateGeneration::Zero:
            return DateGenerationRule::kZero;
        case QuantLib::DateGeneration::ThirdWednesday:
            return DateGenerationRule::kThirdWednesday;
        case QuantLib::DateGeneration::ThirdWednesdayInclusive:
            return DateGenerationRule::kThirdWednesdayInclusive;
        case QuantLib::DateGeneration::Twentieth:
            return DateGenerationRule::kTwentieth;
        case QuantLib::DateGeneration::TwentiethIMM:
            return DateGenerationRule::kTwentiethImm;
        case QuantLib::DateGeneration::OldCDS:
            return DateGenerationRule::kOldCds;
        case QuantLib::DateGeneration::CDS:
            return DateGenerationRule::kCds;
        case QuantLib::DateGeneration::CDS2015:
            return DateGenerationRule::kCds2015;
    }
    std::ostringstream oss;
    oss << "unsupported QuantLib::DateGeneration::Rule value " << static_cast<int>(rule);
    ThrowUnsupported(oss.str());
}

QuantLib::Currency ToQuantLib(const Currency currency) {
    switch (currency) {
        case Currency::kUsd:
            return QuantLib::USDCurrency();
        case Currency::kEur:
            return QuantLib::EURCurrency();
        case Currency::kGbp:
            return QuantLib::GBPCurrency();
        case Currency::kJpy:
            return QuantLib::JPYCurrency();
        case Currency::kChf:
            return QuantLib::CHFCurrency();
        case Currency::kAud:
            return QuantLib::AUDCurrency();
        case Currency::kCad:
            return QuantLib::CADCurrency();
        case Currency::kHkd:
            return QuantLib::HKDCurrency();
        case Currency::kSgd:
            return QuantLib::SGDCurrency();
    }
    ThrowUnsupported("unsupported Currency");
}

Currency FromQuantLib(const QuantLib::Currency& currency) {
    const std::string code = currency.code();
    if (code == "USD") {
        return Currency::kUsd;
    }
    if (code == "EUR") {
        return Currency::kEur;
    }
    if (code == "GBP") {
        return Currency::kGbp;
    }
    if (code == "JPY") {
        return Currency::kJpy;
    }
    if (code == "CHF") {
        return Currency::kChf;
    }
    if (code == "AUD") {
        return Currency::kAud;
    }
    if (code == "CAD") {
        return Currency::kCad;
    }
    if (code == "HKD") {
        return Currency::kHkd;
    }
    if (code == "SGD") {
        return Currency::kSgd;
    }
    ThrowUnsupported("unsupported QuantLib::Currency code '" + code + "'");
}

}  // namespace numeraire::utils::quantlib_bridge
