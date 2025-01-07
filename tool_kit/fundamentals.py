import QuantLib as ql
from tool_kit.quantlib_tool_kit import QuantLibToolKit
from app_settings import AppSettings
from typing import List, Any



class InterestRateFundamentals:



    @staticmethod
    def get_number_of_periods(schedule: ql.Schedule, convention:str) -> (float,List[float]):
        """
        Description
        -----------
        Calculates the number of periods for a given schedule.
        Parameters
        ----------
        schedule: ql.Schedule
        convention: str

        Returns
        -------
        (float,List[float])

        """

        lf_year_fraction: list[Any] = []
        scheduled_dates = list(schedule)
        if len(scheduled_dates) == 2:
            # frequency probably set as 'once' so only valuation and termination date has been provided
            return convention.yearFraction(scheduled_dates[0], scheduled_dates[1])
        for i in range(1, len(scheduled_dates)):
            temp_yf = convention.yearFraction(
                scheduled_dates[i - 1], scheduled_dates[i])
            lf_year_fraction.append(temp_yf)
        return lf_year_fraction

    @staticmethod
    def discount_factor(spot_rate: float, valuation_date: (str, ql.Date), maturity_date: (str, ql.Date),
                        freq_period: str,
                        year_fraction_convention: str = AppSettings.DEFAULT_YEAR_FRACTION_CONVENTION) -> float:
        """
        Description
        -----------
        Calculates the discount factor given for a given spot rate. If you use a valuation date and maturity date, as
        a string pass them in format 'YYYY-MM-DD'

        Parameters
        ----------
        spot_rate:float
        valuation_date:str,ql.Date
        maturity_date:str,ql.Date
        freq_period:str

        Returns
        -------
        float
        """

        if isinstance(valuation_date, str) or isinstance(maturity_date, str):
            valuation_date = QuantLibToolKit.string_2ql_date(valuation_date)
            maturity_date = QuantLibToolKit.string_2ql_date(maturity_date)
        fractions = InterestRateFundamentals.get_year_fraction(
            schedule=QuantLibToolKit.define_schedule(valuation_date=valuation_date, termination_date=maturity_date,
                                                     freq_period=freq_period),
            convention=QuantLibToolKit.set_year_fraction_convention(year_fraction_conv=year_fraction_convention))
        return 1 / (1 + spot_rate) ** fractions

    @staticmethod
    def simple_forward_rate(valuation_date: ql.Date, expiry_date: ql.Date, maturity_date: ql.Date) -> float:
        """
        Description
        -----------
        Calculates the simple forward rate, prevailing at valuation date rate given a discount rate.

        Parameters
        ----------
        valuation_date:ql.Date
        expiry_date:ql.Date
        maturity_date:ql.Date

        Returns
        -------
        float
        """
        pass



