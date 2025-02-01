import matplotlib.pyplot as plt
from typing import List

import pandas as pd
from pandas.plotting import register_matplotlib_converters


class PlotUsingMatplotLib:

    @staticmethod
    def user_formatter(x, scale: str):
        if scale == 'million':
            return str(round(x / 1e6, 1))
        if scale == 'hundred_thousands':
            return str(round(x / 1e5, 1))

    @staticmethod
    def plot(ts_df_list: list[pd.DataFrame],
             ts_labels_list: List[str],
             x_label: str,
             y_label: str,
             nrows: (int, None) = None,
             ncols: (int, None) = None,
             facecolor: (str, None) = None,
             xticks=None,
             figsize=(20, 80),
             adjust={"left": 0.1,
                     "bottom": 0.1,
                     "right": 0.9,
                     "top": 0.9,
                     "wspace": 0.4,
                     "hspace": 0.4}):
        """
          Plots time series data from a list of Pandas DataFrames.

          This function generates either a single plot or multiple subplots based on the provided
          number of rows (`nrows`) and columns (`ncols`). Each time series is expected to be stored
          in a DataFrame where the first column represents dates and the second column represents the
          data to be plotted.

          Parameters:
          ----------
          ts_df_list : list[pd.DataFrame]
              A list of Pandas DataFrames, each containing time series data.
              The first column should contain date values, and the second column should contain
              the corresponding numerical values.

          ts_labels_list : List[str]
              A list of labels for each time series dataset.
              Must have the same length as `ts_df_list`.

          x_label : str
              The label for the x-axis.

          y_label : str
              The label for the y-axis.

          nrows : int, optional
              The number of rows in the subplot grid.
              If `None`, a single plot is generated.

          ncols : int, optional
              The number of columns in the subplot grid.
              If `None`, a single plot is generated.

          facecolor : str, optional
              Background color for the plot. Default is `None`.

          xticks : optional
              Custom tick values for the x-axis. Default is `None`.

          figsize : tuple, optional
              The figure size in inches (width, height). Default is (20, 80).

          adjust : dict, optional
              A dictionary to adjust the subplot layout.
              Default values:
              - "left": 0.1
              - "bottom": 0.1
              - "right": 0.9
              - "top": 0.9
              - "wspace": 0.4
              - "hspace": 0.4

          Returns:
          -------
          tuple
              A tuple `(fig, axes)` where:
              - `fig` is the Matplotlib figure object.
              - `axes` is the Matplotlib Axes object(s).

          Raises:
          ------
          Exception
              - If `ts_df_list` and `ts_labels_list` have different lengths.
              - If any DataFrame in `ts_df_list` does not have at least two columns.
              - If the number of subplots (`nrows * ncols`) is insufficient for the number of time series.

          Example:
          -------
          >>> import pandas as pd
          >>> import numpy as np
          >>> dates = pd.date_range("2024-01-01", periods=10)
          >>> df1 = pd.DataFrame({"Date": dates, "Value": np.random.rand(10)})
          >>> df2 = pd.DataFrame({"Date": dates, "Value": np.random.rand(10)})
          >>> ts_list = [df1, df2]
          >>> labels = ["Series 1", "Series 2"]
          >>> fig, axes = plot(ts_list, labels, "Date", "Value", nrows=1, ncols=2, figsize=(12, 6))
          """

        if nrows is None and ncols is None:

            fig, axes = plt.subplots(figsize=figsize)
            axes.plot(ts_df_list[0])
            axes.set_xlabel(x_label)
            axes.set_ylabel(y_label)
            if facecolor is not None:
                axes.set_facecolor(facecolor)
            return fig, axes

        else:
            # -------------------
            # Region: Input Check
            # -------------------
            if len(ts_df_list) != len(ts_labels_list):
                raise Exception('The lenght of the ts_df_list and ts_labels_list must be the same')
            for ts_df in ts_df_list:
                if not isinstance(ts_df, pd.DataFrame) or ts_df.shape[1] <2:
                    raise Exception("The time series dataframes must have at least two columns where the first column"
                                    "represnts the dates and the second one the data. Make sure dataframes also have"
                                    " at least two columns")
                if ncols*nrows<len(ts_df_list):
                    raise Exception("The number of time series exceeds the available sub-plots number."
                                    " Increase the number of rows or columns in the plot grid")

            # -------------------
            # Region: Input Check
            # -------------------
            fig, axes = plt.subplots(nrows=nrows, ncols=ncols, figsize=figsize)
            axes_iter = iter(axes.reshape(-1))
            labels_iter = iter(ts_labels_list)
            for ts_df in ts_df_list:
                if ts_df.size > 0:
                    axe = axes_iter.next()
                    axe.plot(ts_df.iloc[:, 0], ts_df.iloc[:, 1])
                    axe.set_title(labels_iter.next())
            plt.subplots_adjust(left=adjust.get("left"),
                                bottom=adjust.get("bottom"),
                                right=adjust.get("right"),
                                top=adjust.get("top"),
                                wspace=adjust.get("wspace"),
                                hspace=adjust.get("hspace"))
        return fig, axes
