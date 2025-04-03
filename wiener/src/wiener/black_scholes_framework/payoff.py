from abc import ABC, abstractmethod
import numpy as np

from typing import Union, Tuple, Optional
import matplotlib.pyplot as plt


class Payoff(ABC):
    """
    Pure Payoff class with:
    - Single constructor parameter (__spot_price)
    - Fallback logic ONLY in getter
    - Strict encapsulation
    - No external dependencies
    """

    def __init__(self, spot_price: Optional[float] = None):
        """Initialize with only direct spot price"""
        self.__spot_price = None  # Truly private
        if spot_price is not None:
            self.set_spot_price(spot_price)

    def get_spot_price(self) -> Optional[float]:
        """
        Get spot price with fallback logic:
        1. Return directly set spot price
        2. Try to get from attached option's market data
        3. Return None if no spot available
        """
        if self.__spot_price is not None:
            return self.__spot_price

        try:
            # Try to get from owner's market environment
            if hasattr(self, '_owner') and hasattr(self._owner, 'market_environment'):
                return self._owner.market_environment.market_data['underlying_price']
            # Fallback to owner's trade attributes
            elif hasattr(self, '_owner') and hasattr(self._owner, 'trade_attributes'):
                return self._owner.trade_attributes['underlying_price']
        except (AttributeError, KeyError):
            pass

        return None



    def set_spot_price(self, spot_price: float) -> None:
        """Setter with validation"""
        if not isinstance(spot_price, (int, float)):
            raise TypeError("Spot price must be numeric")
        if spot_price <= 0:
            raise ValueError("Spot price must be positive")
        self.__spot_price = float(spot_price)

    def _attach_owner(self, owner: object):
        """Protected method for owner reference (used by EuropeanOption)"""
        self._owner = owner

    @abstractmethod
    def calculate_payoff(self, spot: Union[float, np.ndarray]) -> Union[float, np.ndarray]:
        """Calculate payoff at given spot price(s)"""
        pass

    @abstractmethod
    def payoff_description(self) -> str:
        """String description of payoff"""
        pass

    def plot_payoff(self,
                    spot_range: Tuple[float, float] = None,
                    range_pct: float = 0.3,
                    num_points: int = 100,
                    **plot_kwargs) -> plt.Figure:
        """
        Plot payoff diagram using get_spot_price() with fallback
        """
        ref_spot = self.get_spot_price()
        if spot_range is None:
            if ref_spot is None:
                raise ValueError("No spot price available")
            spot_min = ref_spot * (1 - range_pct)
            spot_max = ref_spot * (1 + range_pct)
        else:
            spot_min, spot_max = spot_range

        spots = np.linspace(spot_min, spot_max, num_points)
        payoffs = self.calculate_payoff(spots)

        plt.figure(figsize=plot_kwargs.pop('figsize', (10, 6)))
        plt.plot(spots, payoffs, **plot_kwargs)
        plt.title(f"Payoff Diagram\n{self.payoff_description()}")
        plt.xlabel("Spot Price")
        plt.ylabel("Payoff")
        plt.grid(True)

        if ref_spot is not None:
            plt.axvline(x=ref_spot, color='r', linestyle='--',
                        label=f'Spot: {ref_spot:.2f}')
            plt.legend()

        plt.tight_layout()
        return plt


class PlainVanillaPayoff(Payoff):
    """Vanilla option payoff implementation"""

    def __init__(self,
                 strike: float|None=None,
                 option_type: str|None=None,
                 spot_price: Optional[float] = None):
        super().__init__(spot_price)
        self.__strike = None
        self.__option_type = None

    def get_strike(self) -> Optional[float]:
        """
        Get spot price with fallback logic:
        1. Return directly set strike
        2. Try to get from attached option's trade_attributes
        3. Return None if no strike
        """
        if self.__strike is not None:
            return self.__strike

        try:
            # Fallback to owner's trade attributes
            if hasattr(self, '_owner') and hasattr(self._owner, 'trade_attributes'):
                return self._owner.trade_attributes['strike']
        except (AttributeError, KeyError):
            pass

        return None

    def set_strike(self, strike: float):
        if not isinstance(strike, (int, float)):
            raise TypeError("Strike must be numeric")
        if strike <= 0:
            raise ValueError("Strike must be positive")
        self.__strike = float(strike)

    def get_option_type(self) -> str:
        return self.__option_type

    def set_option_type(self, option_type: str):
        if not isinstance(option_type, str):
            raise TypeError("Option type must be string")
        option_type = option_type.lower()
        if option_type not in ['call', 'put']:
            raise ValueError("Option type must be 'call' or 'put'")
        self.__option_type = option_type

    def calculate_payoff(self, spot: Union[float, np.ndarray]) -> Union[float, np.ndarray]:
        if self.__option_type == 'call':
            return np.maximum(spot - self.__strike, 0)
        return np.maximum(self.__strike - spot, 0)

    def payoff_description(self) -> str:
        desc = f"Vanilla {self.__option_type} | Strike: {self.__strike:.2f}"
        spot = self.get_spot_price()
        if spot is not None:
            desc += f" | Spot: {spot:.2f}"
        return desc
