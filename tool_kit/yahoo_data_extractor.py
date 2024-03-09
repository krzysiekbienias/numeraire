import yfinance as yf
import matplotlib.pyplot as plt
from typing import TypeVar, Iterable, Tuple, Dict, List
import QuantLib as ql
import pandas as pd


class YahooDataExtractor:
    def __init__(self, tickers:(str,List[str])="TSLA",
                 start_period:str=None,
                 end_period:str=None):
        """__init__
        Description
        -----------
        Class for extracting equity prices provided by YahooFinance.

        Parameters
        ----------
        tickers : str,List[str], optional
            _description_, by default ["TSLA"]
        start_period : str, optional
            _description_, by default None
        end_period : str, optional
            _description_, by default None
        """

        self._tickers = tickers
        self._start_period=start_period
        self._end_period=end_period

        self.close_prices_df=self.extract_data(tickers=self._tickers,
                                               start_period=self._start_period,
                                               end_period=self._end_period)
        
        
    def extract_data(self,
                    tickers,
                    start_period=None,
                    end_period=None,
                    column_name="Close"):
        """extract_data
        Description
        -----------
        This function retrieves close price of equities for defined scope of trades.
        If start_period and end_period is none then we get the latest price, or prices
        Parameters
        ----------
        tickers : list of strings
            Tickers available on Yahoo Finance
        start_period : string (Year-Month-Day)
            beginning of the period
        end_period : string (Year-Month-Day)
            _description_
        column_name : str, optional
             by default "Close"

        Returns
        -------
        pandas.DataFrame
        
        Examples
        --------
        >>> extract_data(tickers=["AAPL",
        >>>                      "BA",
        >>>                      "KO",
        >>>                      "IBM",
        >>>                      "DIS",
        >>>                      "MSFT"],
        >>>                      start_period="2010-01-01",
        >>>                      end_period="2022-10-30")    
        """

        one_column_ts=None
        if start_period is not None and end_period is not None and start_period!=end_period:
            df_equities = yf.download(tickers=tickers,
                                    start=start_period,
                                    end=end_period)
        elif (start_period is not None and end_period is not None and start_period==end_period) or (start_period is not None and end_period is None):
            if isinstance(tickers,str):
                
                end_period_ql_date_increased_by1=ql.Date(start_period,"%Y-%m-%d")+1

                df_equities = yf.Ticker(tickers[0]).history(start=start_period,end=end_period_ql_date_increased_by1.ISO())
                return {tickers: [df_equities.index[0].date(),df_equities[column_name][0]]}
            else:
                underlier_prices_dict=dict()
                for underlier in tickers:
                    underlier_prices_dict.update({underlier:[df_equities.index[0].date(),df_equities[column_name][0]]})
                return underlier_prices_dict    
        else:
            if len(tickers)==1:
                df_equities = yf.Ticker(tickers[0]).history(period="1d")
                return {tickers[0]: [df_equities.index[0].date(),df_equities["Close"][0]]}
            else:
                raise NotImplementedError("This case is not considered yet.")
        
        

    def df_info(self, df:pd.DataFrame):
        """df_info
        Display basic info about extracted time series.

        Parameters
        ----------
        df : pd.DataFrame
            Data Frame to display info.
        """
        print(df.describe())
        return df.describe()
    

    def basic_statistic(self, df:pd.DataFrame):

        print(df.info())

        