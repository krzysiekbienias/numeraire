#pragma once

#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>
#include <numeraire/schedule/schedule_config.hpp>

namespace numeraire::schedule {

/// Builds `Schedule` instances using QuantLib internally.
class ScheduleGenerator {
   public:
    /// Fluent builder for `ScheduleConfig`.
    class Builder {
       public:
        Builder& WithCalendar(CalendarType calendar_type);
        Builder& WithFrequency(Frequency frequency);
        Builder& WithConvention(DateConvention convention);
        Builder& WithRule(DateGenerationRule rule);
        Builder& WithDayCount(DayCount day_count);
        [[nodiscard]] ScheduleGenerator Build() const;

       private:
        ScheduleConfig config_;
    };

    [[nodiscard]] static Builder NewBuilder() { return Builder{}; }

    [[nodiscard]] Schedule Generate(const Date& start, const Date& end) const;

    [[nodiscard]] double YearFraction(const Date& start, const Date& end) const;

    [[nodiscard]] bool IsBusinessDay(const Date& date) const;

    [[nodiscard]] Date Adjust(const Date& date) const;

    [[nodiscard]] const ScheduleConfig& Config() const noexcept { return config_; }

   private:
    friend class Builder;
    explicit ScheduleGenerator(ScheduleConfig config) : config_(std::move(config)) {}

    ScheduleConfig config_;
};

}  // namespace numeraire::schedule
