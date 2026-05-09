#include <numeraire/schedule/schedule_generator.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule_quantlib.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>

#include <ql/time/period.hpp>
#include <ql/time/schedule.hpp>
#include <string>

namespace numeraire::schedule {

namespace {

void ThrowIfFrequencyUnusableForTenorSchedule(const Frequency frequency) {
    switch (frequency) {
        case Frequency::kNoFrequency:
        case Frequency::kOnce:
            throw numeraire::ScheduleError(
                    "ScheduleGenerator: frequency kNoFrequency/kOnce cannot build a tenor-based schedule");
        case Frequency::kOtherFrequency:
            throw numeraire::ScheduleError("ScheduleGenerator: frequency kOtherFrequency is not supported");
        default:
            return;
    }
}

}  // namespace

ScheduleGenerator::Builder& ScheduleGenerator::Builder::WithCalendar(const CalendarType calendar_type) {
    config_.calendar_type = calendar_type;
    return *this;
}

ScheduleGenerator::Builder& ScheduleGenerator::Builder::WithFrequency(const Frequency frequency) {
    config_.frequency = frequency;
    return *this;
}

ScheduleGenerator::Builder& ScheduleGenerator::Builder::WithConvention(const DateConvention convention) {
    config_.convention = convention;
    return *this;
}

ScheduleGenerator::Builder& ScheduleGenerator::Builder::WithRule(const DateGenerationRule rule) {
    config_.rule = rule;
    return *this;
}

ScheduleGenerator::Builder& ScheduleGenerator::Builder::WithDayCount(const DayCount day_count) {
    config_.day_count = day_count;
    return *this;
}

ScheduleGenerator ScheduleGenerator::Builder::Build() const {
    return ScheduleGenerator(config_);
}

Schedule ScheduleGenerator::Generate(const Date& start, const Date& end) const {
    const QuantLib::Date ql_start = ToQuantLibDate(start);
    const QuantLib::Date ql_end = ToQuantLibDate(end);
    if (ql_start > ql_end) {
        throw numeraire::ScheduleError("ScheduleGenerator::Generate: start date is after end date");
    }
    ThrowIfFrequencyUnusableForTenorSchedule(config_.frequency);

    try {
        const QuantLib::Period tenor(numeraire::utils::quantlib_bridge::ToQuantLib(config_.frequency));
        const QuantLib::Schedule ql_schedule(
                ql_start, ql_end, tenor, numeraire::utils::quantlib_bridge::ToQuantLib(config_.calendar_type),
                numeraire::utils::quantlib_bridge::ToQuantLib(config_.convention),
                numeraire::utils::quantlib_bridge::ToQuantLib(config_.convention),
                numeraire::utils::quantlib_bridge::ToQuantLib(config_.rule), false, QuantLib::Date(),
                QuantLib::Date());
        return ScheduleFromQuantLib(ql_schedule, config_.day_count);
    } catch (const std::exception& ex) {
        throw numeraire::ScheduleError(std::string("ScheduleGenerator::Generate: ") + ex.what());
    }
}

double ScheduleGenerator::YearFraction(const Date& start, const Date& end) const {
    try {
        const QuantLib::DayCounter dc = numeraire::utils::quantlib_bridge::ToQuantLib(config_.day_count);
        return dc.yearFraction(ToQuantLibDate(start), ToQuantLibDate(end));
    } catch (const std::exception& ex) {
        throw numeraire::ScheduleError(std::string("ScheduleGenerator::YearFraction: ") + ex.what());
    }
}

bool ScheduleGenerator::IsBusinessDay(const Date& date) const {
    const QuantLib::Calendar cal = numeraire::utils::quantlib_bridge::ToQuantLib(config_.calendar_type);
    return cal.isBusinessDay(ToQuantLibDate(date));
}

Date ScheduleGenerator::Adjust(const Date& date) const {
    const QuantLib::Calendar cal = numeraire::utils::quantlib_bridge::ToQuantLib(config_.calendar_type);
    const QuantLib::BusinessDayConvention bdc = numeraire::utils::quantlib_bridge::ToQuantLib(config_.convention);
    return FromQuantLibDate(cal.adjust(ToQuantLibDate(date), bdc));
}

}  // namespace numeraire::schedule
