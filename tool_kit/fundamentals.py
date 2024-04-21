import QuantLib as ql
from quantlib_tool_kit import QuantLibToolKit


class InterestRateFundamentals:

    @staticmethod
    def discount_factor(spot_rate: float, valuation_date: (str, ql.Date), maturity_date: (str, ql.Date),
                        freq_period: str) -> float:
        """
        Description
        -----------
        Calculates the discount factor given for a given spot rate. If you use a valuation date and maturity date,as a string
        pass them in format 'YYYY-MM-DD'

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
        schedule = list(QuantLibToolKit.define_schedule(valuation_date=valuation_date,
                                                   termination_date=maturity_date,
                                                   freq_period=freq_period))




        time_to_maturity = valuation_date - maturity_date
        return 1 / (1 + spot_rate) ** time_to_maturity

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

if __name__ == '__main__':
    valuation_date: str = "2023-05-12"
    maturity_date: str = "2024-05-12"


    InterestRateFundamentals.discount_factor(spot_rate=0.05, freq_period="annual", valuation_date=valuation_date,
                                             maturity_date=maturity_date)