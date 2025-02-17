
import matplotlib.pyplot as plt
import numpy as np
import scipy as sc
from scipy import stats
import operator
import QuantLib as ql
import os
import pandas as pd
from datetime import datetime

from .analytical_pricer import EuropeanPlainVanillaOption


class GreeksInterface:
    
    def calculate_delta(self):
        pass
    
    def calculate_theta(self):
        pass
    
    def calculate_vega(self):
        pass
    
    

class Greeks:

    def __init__(self ,derivative :EuropeanPlainVanillaOption):
        self.derivative = derivative



    def delta(self) -> float:
        """delta 

        Function returns derivative with respect to underlier's price

        Returns
        -------
        float
            _description_
        """

        if self.derivative.pricable_dict['payoff'] == 'call':
            delta = stats.norm.cdf(self.d1_fun(), 0, 1)

        else:
            delta = -stats.norm.cdf(-self.d1_fun(), 0, 1)

        return delta

    def gamma(self):  # for call and put is identical
        """gamma 

        Function returs second derivative with resppect to the time.

        Returns
        -------
        float

        """
        gamma = stats.norm.pdf(self.d1_fun(), 0, 1) * np.exp(
            -self.derivative.pricable_dict['dividend'] * self.derivative.pricable_dict['tau']) / (
                        self.derivative.priceable_dict['underlier_price'] * self.derivative.priceable_dict
                    ['volatility'] * np.sqrt(
                    self.derivative.pricable_dict['tau']))

        return gamma

    def vega(self):
        vega = self.derivative.priceable_dict['underlier_price'] * np.exp \
            (-self.derivative.pricable_dict['dividend'] * self.derivative.pricable_dict['tau']) * np.sqrt(
            self.derivative.pricable_dict['tau']) * stats.norm.pdf(self.d1_fun(), 0, 1)

        return vega

    def theta(self):
        if self.derivative.pricable_dict['payoff'] == 'call':
            theta = (-self.derivative.priceable_dict['underlier_price'] * stats.norm.pdf(self.d1_fun(), 0, 1) * 
                     self.derivative.priceable_dict['volatility'] * np.exp(
                -self.derivative.pricable_dict['dividend'] * self.derivative.pricable_dict['tau']) / 2 * np.sqrt(
                self.derivative.pricable_dict['tau']) + self.derivative.pricable_dict['dividend'] * self.derivative.priceable_dict['underlier_price'] * np.exp(
                -self.derivative.pricable_dict['dividend'] *
                self.derivative.pricable_dict['tau']) * stats.norm.pdf(self.d1_fun(), 0,
                                                     1) - self.derivative.pricable_dict['risk_free_rate'] * 
                     self.derivative.pricable_dict['strike'] * np.exp(-self.derivative.pricable_dict['risk_free_rate'] *
                    self.derivative.pricable_dict['tau']) * stats.norm.pdf(self.d2_fun(), 0, 1))
            return theta
        if self._type_option == 'put':
            theta = (-self.derivative.priceable_dict['underlier_price'] * stats.norm.pdf(self.d1_fun(), 0, 1) *
                     self.derivative.priceable_dict['volatility'] * np.exp(
                -self.derivative.pricable_dict['dividend'] * self.derivative.pricable_dict['tau']) / 2 * np.sqrt(
                self.derivative.pricable_dict['tau']) - self.derivative.pricable_dict['dividend'] *
                     self.derivative.priceable_dict['underlier_price'] * np.exp(-self.derivative.pricable_dict['dividend'] *
                self.derivative.pricable_dict['tau']) * stats.norm.pdf(-self.d1_fun(), 0,1)
                - self.derivative.pricable_dict['risk_free_rate'] * self.derivative.pricable_dict['strike']
                     * np.exp(-self.derivative.pricable_dict['risk_free_rate'] *
                              self.derivative.pricable_dict['tau']) * stats.norm.pdf(-self.d2_fun(), 0, 1))
            return theta
