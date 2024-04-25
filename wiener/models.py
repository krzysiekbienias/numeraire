from django.db import models
from django.utils import timezone


# Create your models here.
class TradeBook(models.Model):
    trade_id = models.AutoField(primary_key=True)
    underlier_ticker = models.CharField(max_length=25)
    product_type = models.CharField(max_length=25)
    payoff = models.CharField(max_length=10)
    trade_date = models.DateField()
    trade_maturity = models.DateField()
    strike = models.FloatField()
    dividend = models.FloatField()
    user_id = models.CharField(default="kbienias", max_length=100)


class DerivativePrice(models.Model):
    run_id = models.AutoField(primary_key=True)
    trade_id = models.ForeignKey(TradeBook, on_delete=models.CASCADE)
    valuation_date = models.DateField()
    price_status = models.CharField(max_length=25)
    analytical_price = models.FloatField()
    extra_price = models.FloatField()
    created_at = models.DateTimeField(default=timezone.now)
    user_id = models.CharField(default="kbienias", max_length=100)
