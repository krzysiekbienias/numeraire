from tool_kit.fundamentals import InterestRateFundamentals



if __name__ == '__main__':
    valuation_date: str = "2023-05-12"
    maturity_date: str = "2024-05-12"
    spot_rate=0.02

    discount_factor=InterestRateFundamentals.discount_factor(spot_rate=spot_rate, freq_period="annual", valuation_date=valuation_date,
                                             maturity_date=maturity_date)
    print("THE END")
