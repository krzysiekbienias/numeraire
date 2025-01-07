from datetime import datetime
from typing import TypeVar, Dict, List, NewType

import QuantLib as ql

QL = NewType("QL", ql)
DT = TypeVar('DT', bound=datetime)
HM = TypeVar("HM", bound=Dict)


class QuantLibToolKit:
    _weekday_corrections = {"Following": ql.Following,
                            "ModifiedFollowing": ql.ModifiedFollowing,
                            "Preceding": ql.ModifiedPreceding,
                            "ModifiedPreceding": ql.ModifiedPreceding,
                            "Unadjusted": ql.Unadjusted

                            }

    _date_generation_rules = {"Backward": ql.DateGeneration.Backward,
                              "Forward": ql.DateGeneration.Forward}

    @staticmethod
    def date_object_2ql_date(date: datetime.date) -> ql.Date:
        """
        date_object_2qlDate
        Description
        -----------
        function converts datetime object into QuantLib date.
        Parameters
        ----------
        date : datetime.date

        Returns
        -------
        ql.Date
        """
        return ql.Date(date.day, date.month, date.year)

    @staticmethod
    def string_2ql_date(date: str) -> QL:
        """string_2qlDate
        Description
        -----------
        function converts string date representation and convert to equivalent quantlib object.
        Date must be in form Y-m-d.
        Parameters
        ----------
        date : str
            date time object that going to be converted. Must be passed as year, month, day

        Returns
        -------
        QL
            date as a QuantLib object
        """

        dt_date = datetime.strptime(date, "%Y-%m-%d")
        ql_date = ql.Date(dt_date.day, dt_date.month, dt_date.year)
        return ql_date

    @staticmethod
    def set_calendar(country: str = "theUK") -> QL:
        """set_calendar
        Description
        -----------
        Define a calendar for QuantLib.

        Parameters
        ----------
        country : str
            Name of country according to business operates. Currently following calendars are available:
            - USA
            - UK
            - Switzerland
            - Poland

        Returns
        -------
        ql.Calendar
        """
        if country not in ("USA", "theUK", "Switzerland", "Poland"):
            raise ValueError("calendar name must be one of the following: 'USA', 'theUK', 'Switzerland', 'Poland'")
        if country == 'USA':
            return ql.UnitedStates(ql.UnitedStates.NYSE)
        if country == 'theUK':
            return ql.UnitedKingdom()
        if country == 'Switzerland':
            return ql.Switzerland()
        if country == 'Poland':
            return ql.Poland()

    @staticmethod
    def set_date_corrections_schema(corrections_map: HM = _weekday_corrections,
                                    correction_rule: str = "Following") -> int:

        """setBusinessConvention
        Description
        -----------
         It is a day-count convention that adjusts the date to the next valid business day if it falls on a holiday or
        weekend.

        Returns
        -------

            _description_
        """
        return corrections_map[correction_rule]

    @staticmethod
    def set_rule_of_date_generation(date_generation_rules: str = "forward") -> int:

        """set_rule_of_date_generation
        Description
        -----------
        A wrapper to control ql.DataGeneration enumeration to control how a schedule of dates is generated relative to
        start date. With ql.DateGeneration.Backward, the schedule is generated starting from the end date and working
        backwards to the start date. This is often used in financial instruments like bond schedules or swap schedules
        where the last payment date is fixed, and the previous dates (coupon or payment dates) are determined by moving
        backward from that date.

        Returns
        -------
        _type_
            _description_
        """
        if date_generation_rules == "forward":
            return ql.DateGeneration.Forward
        elif date_generation_rules == "backward":
            return ql.DateGeneration.Backward

    @staticmethod
    def set_frequency(freq_period: str) -> int:
        """set_frequency
        Description
        -----------
        This function set frequency period for calendar schedule.

        Parameters
        ----------
        freq_period : str
            one of possible frequency period.

        Returns
        -------
        QL
            frequency Period
        """
        if freq_period not in ('daily', "once", "monthly", "quarterly", "annual", "semiannual"):
            raise ValueError(
                "Name of the period must be one of following 'daily', 'once','monthly','quarterly','annual',"
                "'semiannual'. ")

        if freq_period == 'daily':
            return ql.Daily
        if freq_period == 'once':
            return ql.Once
        if freq_period == 'monthly':
            return ql.Monthly
        if freq_period == 'quarterly':
            return ql.Quarterly
        if freq_period == 'annual':
            return ql.Annual
        if freq_period == 'semiannual':
            return ql.Semiannual

    @staticmethod
    def set_year_fraction_convention(year_fraction_conv: str = "Actual365") -> QL:
        """set_year_fraction_convention
        Description
        -----------
        Returns year fraction convention.

        Parameters
        ----------
        year_fraction_conv : string

        Returns
        -------
        ql.FractionConvention

        """
        if year_fraction_conv == 'Actual360':
            return ql.Actual360()
        elif year_fraction_conv == 'Actual365':
            return ql.Actual365Fixed()
        elif year_fraction_conv == 'ActualActual':
            return ql.ActualActual()
        elif year_fraction_conv == 'Thirty360':
            return ql.Thirty360()
        elif year_fraction_conv == 'Business252':
            return ql.Business252()

    @staticmethod
    def define_schedule(valuation_date: str,
                        termination_date: str,
                        freq_period: str = "monthly",
                        calendar: str = "theUK",
                        correction_rule: str = "Following") -> ql.Schedule:
        """define_schedule
        Description
        -----------
        ql.MakeSchedule is a factory function that creates a schedule based on the parameters passed to it.
         It is essentially a more flexible way to create a Schedule object.

        Parameters
        ----------

        valuation_date : str
            _description_
        termination_date : str
            _description_
        freq_period : str, optional
            _description_, by default "monthly"
        calendar : str, optional
            _description_, by default "theUK"
        correction_rule : str

        Returns
        -------
        ql.Schedule
            _description_
        """
        if isinstance(valuation_date, str) or isinstance(termination_date, str):
            return ql.MakeSchedule(effectiveDate=QuantLibToolKit.string_2ql_date(valuation_date),
                                   terminationDate=QuantLibToolKit.string_2ql_date(termination_date),
                                   frequency=QuantLibToolKit.set_frequency(freq_period),
                                   calendar=QuantLibToolKit.set_calendar(calendar),
                                   convention=QuantLibToolKit.set_date_corrections_schema(
                                       corrections_map=QuantLibToolKit._weekday_corrections,
                                       correction_rule=correction_rule),
                                   endOfMonth=True)
        else:
            return ql.MakeSchedule(effectiveDate=valuation_date,
                                   terminationDate=termination_date,
                                   frequency=QuantLibToolKit.set_frequency(freq_period),
                                   calendar=QuantLibToolKit.set_calendar(calendar),
                                   convention=QuantLibToolKit.set_date_corrections_schema(
                                       corrections_map=QuantLibToolKit._weekday_corrections,
                                       correction_rule=correction_rule),
                                   endOfMonth=True)

    @staticmethod
    def display_schedule(schedule: ql.Schedule):
        print(list(schedule))

    @staticmethod
    def consecutive_year_fraction(schedule: ql.Schedule, day_count: ql.DayCounter = None) -> List[float]:
        if day_count is None:
            day_count = ql.Actual365Fixed()
        yf_arr = []
        for i in range(1, len(schedule)):
            yf_arr.append(day_count.yearFraction(schedule[i - 1], schedule[i]))
        return yf_arr

    @staticmethod
    def cumulative_year_fraction(schedule: ql.Schedule, day_count: ql.DayCounter = None) -> List[float]:
        if day_count is None:
            day_count = ql.Actual365Fixed()
        yf_arr = []
        for i in range(1, len(schedule)):
            yf_arr.append(day_count.yearFraction(schedule[0], schedule[i]))
        return yf_arr
