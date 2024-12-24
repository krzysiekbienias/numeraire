import matplotlib.pyplot as plt
from typing import List

import pandas as pd


class PlotUsingMatplotLib:

    @staticmethod
    def user_formatter(x,scale:str):
        if scale == 'million':
            return str(round(x/1e6,1))
        if scale=='hundred_thousands':
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
             figsize=(20,80)):
        if nrows is None and ncols is None:

            fig, axes = plt.subplots(figsize=figsize)
            axes.plot(ts_df_list[0])
            axes.set_xlabel(x_label)
            axes.set_ylabel(y_label)
            if facecolor is not None:
                axes.set_facecolor(facecolor)

        else:
            NotImplementedError("Multi Grid plotting not implemented yet")
