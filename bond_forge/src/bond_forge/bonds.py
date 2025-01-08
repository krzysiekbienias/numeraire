import QuantLib as ql

from tool_kit.quantlib_tool_kit import QuantLibToolKit


class BondsInterface:

    def __init__(self):
        pass

    def fetch_bond_details(self, id):
        pass

    def define_schedule(self):
        pass

    def create_bond(self):
        pass

    def price(self):
        pass

    def yield_to_maturity(self, market_price, initial_quess, tolerance, max_iteration):
        pass


class FixedRateBond:
    coupon_divider = dict(quarterly=4, semiannually=2, monthly=12)

    def __init__(self):
        self.id:int = 1
        self.face_value: float = 1000
        self.coupon_rate: float = 0.05
        self.coupon_frequency: str = 'quarterly'
        self.issue_date: str = '2020-01-01'
        self.maturity_date: str = '2030-01-01'
        self.is_callable: bool = False
        self.call_price: (float, None)

    def create_bond(self, face_value: float,
                    coupon_rate: float,
                    coupon_frequency: str,
                    maturity_date: str,
                    issue_date: str,
                    is_callable: bool,
                    call_price: (float, None)):
        self.face_value = face_value
        self.coupon_rate = coupon_rate
        self.maturity_date = maturity_date
        self.issue_date = issue_date
        self.coupon_frequency = coupon_frequency
        self.is_callable = is_callable
        if self.is_callable:
            self.call_price = call_price
        else:
            self.call_price = None



    def create_payment_schedule(self):
        # Convert string dates to QuantLib.Date
        issue_date_ql = ql.DateParser.parseISO(self.issue_date)
        maturity_date_ql = ql.DateParser.parseISO(self.maturity_date)
        # Validate the dates
        if issue_date_ql > maturity_date_ql:
            raise ValueError("Issue date must be earlier than maturity date")

        schedule: ql.Schedule = QuantLibToolKit().define_schedule(valuation_date=self.issue_date,
                                                                  termination_date=self.maturity_date,
                                                                  freq_period='quarterly'
                                                                  )
        return schedule

    def periodic_coupon(self):
        return self.coupon_rate * self.face_value / FixedRateBond.coupon_divider[self.coupon_frequency]

    def derivative_price(self, yield_to_maturity):
        coupon_derivative = 0
        schedule = self.create_payment_schedule()
        cashflow_periods = QuantLibToolKit.cumulative_year_fraction(schedule=schedule)
        coupon_value = self.periodic_coupon()
        for t in range(len(cashflow_periods)):
            coupon_derivative += -t * coupon_value / (1 + yield_to_maturity) ** (1 + cashflow_periods[t])
        pv_derivative = self.face_value / (1 + yield_to_maturity) ** cashflow_periods[-1]
        return coupon_derivative + pv_derivative

    def yield_to_maturity(self, market_price: float, initial_quess=0.03,
                          tolerance=1e-6, max_terations=1000):

        pass

    def price(self, yield_to_maturity: float):
        pv_coupons=0
        schedule = self.create_payment_schedule()
        cashflow_periods = QuantLibToolKit.cumulative_year_fraction(schedule=schedule)
        coupon_value = self.periodic_coupon()
        for t in range(len(cashflow_periods)):
            pv_coupons += coupon_value / (1 + yield_to_maturity) ** cashflow_periods[t]
        pv_principal = self.face_value / (1 + yield_to_maturity) ** cashflow_periods[-1]
        return pv_coupons + pv_principal


if __name__ == '__main__':
    bo = FixedRateBond(1)
    schedule_bo = bo.create_payment_schedule()
    bo.price(0.03)
    bo.derivative_price(0.03)

    print('the end')
