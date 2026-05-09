#pragma once

#include <numeraire/enums/calendar_type.hpp>
#include <numeraire/enums/currency.hpp>
#include <numeraire/enums/date_convention.hpp>
#include <numeraire/enums/date_generation_rule.hpp>
#include <numeraire/enums/day_count.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/frequency.hpp>
#include <numeraire/enums/option_type.hpp>

#include <ql/currency.hpp>
#include <ql/exercise.hpp>
#include <ql/option.hpp>
#include <ql/time/businessdayconvention.hpp>
#include <ql/time/calendar.hpp>
#include <ql/time/dategenerationrule.hpp>
#include <ql/time/daycounter.hpp>
#include <ql/time/frequency.hpp>

namespace numeraire::utils::quantlib_bridge {

[[nodiscard]] QuantLib::Option::Type ToQuantLib(OptionType option_type);
[[nodiscard]] OptionType FromQuantLib(QuantLib::Option::Type option_type);

[[nodiscard]] QuantLib::Exercise::Type ToQuantLib(ExerciseStyle style);
[[nodiscard]] ExerciseStyle FromQuantLib(QuantLib::Exercise::Type exercise_type);

[[nodiscard]] QuantLib::Calendar ToQuantLib(CalendarType calendar_type);
[[nodiscard]] CalendarType FromQuantLib(const QuantLib::Calendar& calendar);

[[nodiscard]] QuantLib::Frequency ToQuantLib(Frequency frequency);
[[nodiscard]] Frequency FromQuantLib(QuantLib::Frequency frequency);

[[nodiscard]] QuantLib::DayCounter ToQuantLib(DayCount day_count);
[[nodiscard]] DayCount FromQuantLib(const QuantLib::DayCounter& day_counter);

[[nodiscard]] QuantLib::BusinessDayConvention ToQuantLib(DateConvention convention);
[[nodiscard]] DateConvention FromQuantLib(QuantLib::BusinessDayConvention convention);

[[nodiscard]] QuantLib::DateGeneration::Rule ToQuantLib(DateGenerationRule rule);
[[nodiscard]] DateGenerationRule FromQuantLib(QuantLib::DateGeneration::Rule rule);

[[nodiscard]] QuantLib::Currency ToQuantLib(Currency currency);
[[nodiscard]] Currency FromQuantLib(const QuantLib::Currency& currency);

}  // namespace numeraire::utils::quantlib_bridge
