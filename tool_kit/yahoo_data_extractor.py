import yfinance as yf
import matplotlib.pyplot as plt
from typing import TypeVar, Iterable, Tuple, Dict, List
import QuantLib as ql
import pandas as pd


class YahooDataExtractor:
    def __init__(self, tickers: (str, List[str]) = "TSLA",
                 start_period: str = None,
                 end_period: str = None):
        '''
        Description
        ----------

        Extract data from Yahoo finance.
        Parameters
        ----------
        tickers
        start_period
        end_period
        '''

        self._tickers = tickers
        self._start_period = start_period
        self._end_period = end_period

    def extract_data(self,
                     column_name="Close"):
        """
        extract_data
        Description
        -----------
        Extract data from Yahoo finance by tricker or set of tickers.
        Parameters
        ----------
        column_name

        Returns
        -------

        """
        underlier_prices_dict = dict()
        # different tickers and time window
        if self._start_period is not None and self._end_period is not None and self._start_period != self._end_period \
            and isinstance(self._tickers, List):
            for ticker in self._tickers:
                df_equities = yf.download(tickers=ticker,
                                          start=self._start_period,
                                          end=self._end_period)[[column_name]]
                underlier_prices_dict[ticker] = df_equities
            return underlier_prices_dict

        # one ticker and time stamp
        elif (self._start_period is not None and self._end_period is not None and
            self._start_period == self._end_period) or (self._start_period is not None and self._end_period is None):
            if isinstance(self._tickers, str):

                end_period_ql_date_increased_by1 = ql.Date(self._start_period, "%Y-%m-%d") + 1

                df_equities = yf.Ticker(self._tickers).history(start=self._start_period,
                                                                  end=end_period_ql_date_increased_by1.ISO())
                underlier_prices_dict[self._tickers] = [df_equities.index[0].date(), df_equities.iloc[0, 3]]
                return underlier_prices_dict
            # the newest available prices starting from start date.
            elif self._start_period is not None and self._end_period=='newest':

                end_period_ql_date_increased_by1 = ql.Date(self._start_period, "%Y-%m-%d") + 1

                for underlier in self._tickers:
                    df_equities = yf.Ticker(underlier).history(start=self._start_period,
                                                                      end=end_period_ql_date_increased_by1.ISO())
                    underlier_prices_dict.update(
                        {underlier: [df_equities.index[0].date(), df_equities[column_name][0]]})
                return underlier_prices_dict
            else:
                raise NotImplementedError("Another cases are not implemented yet")

    @staticmethod
    def df_info(df: pd.DataFrame):
        """df_info
        Display basic info about extracted time series.

        Parameters
        ----------
        df : pd.DataFrame
            Data Frame to display info.
        """
        print(df.describe())
        return df.describe()

    @staticmethod
    def basic_statistic(df: pd.DataFrame):

        print(df.info())
