from typing import TypeVar, List, NewType

import QuantLib as ql
import pandas as pd

from tool_kit.quantlib_tool_kit import QuantLibToolKit
from tool_kit.yahoo_data_extractor import YahooDataExtractor
from ...models import TradeBook

QL = NewType("QL", ql)

DT = TypeVar('DT')


class TradeCalendarSchedule:

    def __init__(self,
                 valuation_date: str = "2023-4-22",
                 termination_date: str = "2023-7-22",
                 calendar: str = "theUK",
                 year_fraction_convention: str = "Actual365",
                 frequency: str = "monthly",
                 **kwargs):
        """
        Description
        -----------
        This class sets up generic the environment for pricing different financial instruments.

        Parameters
        ----------
        valuation_date : str
            The date you want to price financial instrument. Format Y-m-d.

        termination_date : str
            Date after which your trade expires. Format Y-m-d.
        calendar : str, optional
            Name of the specific calendar of dates to follow, by default "theUK". It corresponds with name of country.
        Possible calendar to chose:
        - theUk
        - USA
        - Poland
        - Switzerland
        year_fraction_convention : str, optional
            Name of year count convention that sets rule how year fraction to be calculated, by default "Actual365"
            Another possible:
            - Actual360, 
            - Actual365,
            - ActualActual,
            - Thirty360
            - Business252 
        frequency : str, optional
            Name of frequency according to with tenors are generated. In particular daily if we want to have business days in our schedule,
              by default "monthly"
            Possible parameters:
            - daily
            - once
            - monthly
            - quarterly
            - annual
            - semiannual


        Note
        ----
        Please note that within this class as well as QuantLibToolKid are defined different static methods where variable are converted into.
        This interface allows to User do not take care about it.

        """

        # -----------------------
        # Region: Constructors
        # -----------------------
        self._valuation_date = valuation_date
        self._termination_date = termination_date
        self._s_calendar = calendar
        self._s_year_fraction_conv = year_fraction_convention
        self._s_frequency = frequency
        # -----------------------
        # END Region: Constructors
        # -----------------------

        # ----------------------------
        # Region: Check format of dates
        # ----------------------------
        if not isinstance(self._termination_date, str):
            #probably we have datetime type and we need to convert to str to unify.
            self._termination_date = self._termination_date.strftime("%Y-%m-%d")

        if not isinstance(self._valuation_date, str):
            #probably we have datetime type and we need to convert to str to unify.
            self._valuation_date = self._valuation_date.strftime("%Y-%m-%d")

        # ----------------------------
        # End region: Check format of dates
        # ----------------------------

        # ----------------------------
        # Region: Processing input data
        # ----------------------------

        self.pricing_dictionary = self.process_trade_info_data(trade_id=1)

        # -----------------------
        # END Processing input data
        # -----------------------

        # -----------------------
        # Region: QuantLib Converter
        # -----------------------
        ql_valuation_date = QuantLibToolKit.string_2ql_date(
            date=self._valuation_date)
        ql_termination_date = QuantLibToolKit.string_2ql_date(
            date=self._termination_date)
        ql_year_fraction_conv = QuantLibToolKit.set_year_fraction_convention(
            year_fraction_conv=self._s_year_fraction_conv)
        ql_calendar = QuantLibToolKit.set_calendar(country=self._s_calendar)

        ql_schedule = self.set_schedule(effective_date=ql_valuation_date,
                                        termination_date=ql_termination_date,
                                        tenor=ql.Period(QuantLibToolKit.set_frequency(
                                            freq_period=self._s_frequency)),
                                        calendar=ql_calendar)
        # -----------------------
        # Region: QuantLib Converter
        # -----------------------

        # -----------------------
        # Region: Attributes
        # -----------------------
        self.scheduled_dates = list(ql_schedule)
        self.year_fractions = self.get_year_fraction_sequence(schedule=ql_schedule,
                                                              convention=ql_year_fraction_conv)
        self.days_until_expiration = self.scheduled_dates[-1] - self.scheduled_dates[0]
        # -----------------------
        # End Region: Attributes
        # -----------------------

    # -----------------------
    # Region: getters
    # -----------------------
    def get_valuation_date(self):
        return self._valuation_date

    def get_termination_date(self):
        return self._termination_date

    def get_calendar_name(self):
        return self._s_calendar

    def get_year_fraction_convention_name(self):
        return self._s_year_fraction_conv

    def get_frequency(self):
        return self._s_frequency

    # -----------------------
    # End Region: getters
    # -----------------------

    # -----------------------
    # Region: setters
    # -----------------------        
    def set_valuation_date(self, valuation_date):
        self._valuation_date = valuation_date

    def set_termination_date(self, termination_date):
        self._termination_date = termination_date

    def set_calendar(self, calendar):
        self._s_calendar = calendar

    def set_year_fraction_convention_name(self, year_fraction_convention):
        self._s_year_fraction_conv = year_fraction_convention

    def set_frequency(self, frequency):
        self._s_frequency = frequency

    # -----------------------
    # END Region: setters
    # -----------------------

    def process_trade_info_data(self, trade_id=None, *args, **kwargs):
        if trade_id is None:
            # valuation_date     
            valuation_date = args[0] if len(args) > 0 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_valuation_date()
            # termination_date
            termination_date = args[1] if len(args) > 1 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_termination_date()
            # calendar_name
            calendar_name = args[2] if len(args) > 2 else kwargs[
                "calendar_name"] if "calendar_name" in kwargs else self.get_calendar_name()
            # year_convention
            year_convention = args[3] if len(args) > 3 else kwargs[
                "year_convention"] if "year_convention" in kwargs else self.get_year_fraction_convention_name()
            # frequency
            year_frequency = args[4] if len(args) > 4 else kwargs[
                "year_frequency"] if "year_frequency" in kwargs else self.get_frequency()

            params_set = {"trade_id": None,
                          "valuation_date": valuation_date,
                          "termination_date": termination_date,
                          "calendar_name": calendar_name,
                          "year_convention": year_convention,
                          "year_frequency": year_frequency,
                          "strike": None,
                          "underlying_ticker": "dummy_compnay"}

            return params_set
        else:
            # we derive some trade info from data base
            self.parse_trade_attributes_from_db(trade_id=trade_id)
            valuation_date = args[0] if len(args) > 0 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_valuation_date()

    @staticmethod
    def parse_trade_attributes_from_db(trade_id: int):

        one_trade_from_db = TradeBook.objects.get(pk=trade_id)

        # valuation_date

        params_set = {"trade_id": one_trade_from_db.trade_id,
                      "termination_date": one_trade_from_db.trade_maturity,
                      "calendar_name": None,
                      "year_convention": None,
                      "year_frequency": None,
                      "strike": one_trade_from_db.strike,
                      "underlying_ticker": one_trade_from_db.underlying_ticker,
                      "payoff": one_trade_from_db.payoff,
                      "product_type": one_trade_from_db.product_type,
                      "dividend": one_trade_from_db.dividend
                      }

        return params_set

    @staticmethod
    def set_schedule(effective_date: ql.Date,
                     termination_date: ql.Date,
                     tenor: ql.Period,
                     calendar: ql.Calendar,
                     convention=QuantLibToolKit.set_date_corrections_schema(),
                     termination_date_convention=QuantLibToolKit.set_date_corrections_schema(),
                     rule: ql.DateGeneration = QuantLibToolKit.set_rule_of_date_generation(
                         date_generation_rules="forward"),
                     endOfMonth: bool = False) -> ql.Schedule:
        """set_schedule
        Description
        -----------
        Method creates the schedule that handle with life cycle of a trade. The heart of the class.

        Parameters
        ----------
        effective_date : ql.Date
            _description_
        termination_date : ql.Date
            _description_
        tenor : ql.Period
            _description_
        calendar : ql.Calendar
            _description_
        convention : _type_, optional
            _description_, by default QuantLibToolKit.set_date_corrections_schema()
        termination_date_convention : _type_, optional
            _description_, by default QuantLibToolKit.set_date_corrections_schema()
        rule : ql.DateGeneration, optional
            _description_, by default QuantLibToolKit.setRuleOfDateGeneration()
        endOfMonth : bool, optional
            _description_, by default False

        Returns
        -------
        ql.Schedule
            _description_
        """

        return ql.Schedule(effective_date, termination_date, tenor, calendar, convention, termination_date_convention,
                           rule, endOfMonth)

    def get_year_fraction_sequence(self,
                                   schedule: ql.Schedule,
                                   convention: QL) -> List[float]:
        """consecutiveDatesYearFraction
        Description
        -----------
        For given sequence of dates returns arr of list year fractions with edges T_i-1 and T_i


        Returns
        -------
        List[float]
            list of year fraction's
        """
        lf_year_fraction = []
        scheduled_dates = list(schedule)
        if len(scheduled_dates) == 2:
            # frequency probably set as 'once' so only valuation and termination date has been provided
            return convention.yearFraction(scheduled_dates[0], scheduled_dates[1])
        for i in range(1, len(scheduled_dates)):
            temp_yf = convention.yearFraction(
                scheduled_dates[i - 1], scheduled_dates[i])
            lf_year_fraction.append(temp_yf)
        return lf_year_fraction

    def display_schedule(self) -> None:
        """display_schedule
        Description
        -----------
        This method display information about schedule.
        """

        dates = self.scheduled_dates
        if len(dates) == 2:
            print("At valuation date there is still {0:.2f} of the year.".format(
                self.year_fractions))
        else:
            year_fractions = [None] + self.year_fractions
            df_schedule = pd.DataFrame(zip(dates, year_fractions), columns=[
                'Calendar_Grid', "Year_Fraction"])
            print(df_schedule)


class MarketEnvironmentInterface:
    def extract_underlying_quotation(self):
        pass

    def extract_volatility(self):
        pass

    def extract_discount_factors(self):
        pass

    def validate_market_data(self):
        pass

    def process_parameters(self):
        pass


class MarketEnvironmentHandler:
    def __init__(self,
                 valuation_date: str = "2023-10-02",  #remember to add zero for day
                 underlying_name: str = "TSLA",
                 underlying_price: float = 100,
                 risk_free_rate: float = 0.06,
                 volatility: float = 0.32,
                 discount_factor: float = 0.99,
                 trade_id: (int, None) = None
                 ):
        """
        __init__ _summary_

        Parameters
        ----------
        valuation_date : str, optional
            _description_, by default "2023-10-02"
        risk_free_rate : float, optional
            _description_, by default 0.06
        volatility : float, optional
            _description_, by default 0.32
        trade_id : int, optional
            _description_, by default 1
        """

        self._valuation_date = valuation_date
        self._underlying_price = underlying_price
        self._underlying_name = underlying_name
        self._risk_free_rate = risk_free_rate
        self._volatility = volatility
        self._discount_factor = discount_factor,
        self._trade_id = trade_id

        self.market_data: dict = dict()

    # --------------
    # Region getters
    # --------------

    def get_trade_id(self):
        return self._trade_id

    def get_valuation_date(self):

        return self._valuation_date

    def get_underlying_name(self):
        return self._underlying_name

    def get_risk_free_rate(self):
        return self._risk_free_rate

    def get_volatility(self):
        return self._volatility

    # --------------
    # End  Region getters
    # --------------

    # --------------
    # Region setters
    # --------------
    def set_valuation_date(self, valuation_date):
        self._valuation_date = valuation_date

    def set_underlying_name(self, underlying_name):
        if self._trade_id is None:
            self._underlying_name = underlying_name
        else:
            self._underlying_name = self.parse_underlying_price_from_yahoo_finance(trade_id=self._trade_id)['db_name']

    def set_underlying_price(self, underlying_price):
        if self._trade_id is None:
            self._underlying_price = underlying_price
        else:
            self._underlying_price = self.parse_underlying_price_from_yahoo_finance(trade_id=self._trade_id)[
                'underlying_price']

    def set_risk_free_rate(self, risk_free_rate):
        self._risk_free_rate = risk_free_rate

    def set_volatility(self, volatility):
        self._volatility = volatility

    # --------------
    # End  Region getters
    # --------------

    @staticmethod
    def parse_trade_ticker_by_trade_id(trade_id: (int, None)) -> str:
        """
        parse_trade_ticer_by_trade
        Description
        -----------
        This method parses ticker from db based on trade_id


        Parameters
        ----------
        trade_id : int

        Returns
        -------
        str
        """
        if trade_id is not None and TradeBook.objects.filter(pk=trade_id).exists():
            underlying_for_trade = TradeBook.objects.get(pk=trade_id).underlying_ticker
            return underlying_for_trade
        else:
            return None

    def parse_underlying_price_from_yahoo_finance(self, trade_id: (int, None)) -> dict:
        """
        parse_underlying_price_from_db_info
        Description
        -----------
        This method check if trade exists in db and if so extract underlying price, by calling self.extract_underlying_quotation().

        Parameters
        ----------
        trade_id : int,None

        Returns
        -------
        float
            
        """

        if TradeBook.objects.filter(pk=trade_id).exists():
            print(f"Trade {trade_id} exists ")
            underlying_for_trade = TradeBook.objects.get(pk=trade_id).underlying_ticker
            print(f"You might extract underlying price for {underlying_for_trade}")
            underlying_price = self.extract_underlying_quotation()
            return {"db_underlying": underlying_for_trade, "underlying_price": underlying_price}
        else:
            print(f"Thre is no trade with id {trade_id}")
            return None

    def create_ql_rate(self, risk_free_rate: float = 0.05, year_fraction_conv: ql.DayCounter = ql.Actual365Fixed(),
                       compounding_type: int = ql.Continuous, frequency: int = ql.Once) -> ql.InterestRate:

        return ql.InterestRate(risk_free_rate, year_fraction_conv, compounding_type, frequency)

    def extract_underlying_quotation(self) -> dict:
        """
        extract_underlying_quotation
        Description
        -----------

        Returns
        -------
        dict
            _description_
        """
        if self._trade_id is None:

            return YahooDataExtractor(tickers=self._underlying_name, start_period=self._valuation_date).close_prices_df
        else:
            return YahooDataExtractor(tickers=self.parse_trade_ticker_by_trade_id(trade_id=self._trade_id),
                                      start_period=self._valuation_date).close_prices_df

    def extract_discount_factors(self, maturity_date: (str, None) = None) -> float:
        """
        Description
        ----------
        This method calculates discount factors based on maturity date and valuation date.

        Parameters
        ----------
        maturity_date
        """
        rate = self.create_ql_rate(risk_free_rate=self._risk_free_rate)
        if self._trade_id is not None:

            self._discount_factor = rate.discountFactor(QuantLibToolKit.string_2ql_date(self._valuation_date),
                                                        QuantLibToolKit.date_object_2ql_date(
                                                            TradeBook.objects.get(pk=self._trade_id).trade_maturity))
        else:
            self._discount_factor = rate.discountFactor(QuantLibToolKit.string_2ql_date(self._valuation_date),
                                                        QuantLibToolKit.string_2ql_date(maturity_date))

    def upload_market_data(self,  **kwargs) -> dict:
        """
        Description
        -----------
         method to parse option price that comes from market. It supports defining parameters by user when the
        method is called as well as feeding parameters from diverse data providers. ** kwargs passed to process

        - underlying_ticker
        - underlying_price
        - market_date
        - risk_free_rate
        - volatility
        Returns
        -------
        dict
        """

        # risk-free rate

        risk_free_rate = kwargs['risk_free_rate'] if "risk_free_rate" in kwargs else self.get_risk_free_rate()
        # volatility
        volatility =  kwargs['volatility'] if "volatility" in kwargs else self.get_volatility()
        # underlying name
        if self._trade_id is not None:
            underlying_name = self.parse_trade_ticker_by_trade_id(trade_id=self._trade_id)

        else:
            underlying_name = self.get_underlying_name()
        # underlying price at valuation date
        underlying_price = kwargs["underlying_price"] if "underlying_price" in kwargs \
            else self.extract_underlying_quotation()[underlying_name][1]

        discount_factor = kwargs["discount_factor"] if "discount_factor" in kwargs else self.extract_discount_factors()

        self.market_data = dict(underlying_price=underlying_price,
                                risk_free_rate=risk_free_rate,
                                volatility=volatility,
                                underlying_name=underlying_name,
                                discount_factor=discount_factor)

    def validate_market_data(self):
        raise NotImplementedError()

    def display_market_data(self):
        def __str__(self) -> str:
            return f'Market data:\n underlying Price :{self._underlying_price} \n Risk free rate for pricing :{self._risk_free_rate}\n Implied volatility: {self._implied_volatility}\n '
