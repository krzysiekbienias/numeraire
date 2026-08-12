# Generated manually for futures_contract catalog mirror.
# Schema owned by sql/schema_v1.sql.

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0006_futures_product'),
    ]

    operations = [
        migrations.CreateModel(
            name='FuturesContract',
            fields=[
                ('id', models.AutoField(primary_key=True, serialize=False)),
                ('ticker', models.TextField()),
                ('listing_as_of', models.DateField()),
                ('product_code', models.TextField()),
                ('name', models.TextField(blank=True, null=True)),
                ('active', models.IntegerField(blank=True, null=True)),
                ('type', models.TextField(blank=True, null=True)),
                ('trading_venue', models.TextField(blank=True, null=True)),
                ('group_code', models.TextField(blank=True, null=True)),
                ('first_trade_date', models.TextField(blank=True, null=True)),
                ('last_trade_date', models.TextField(blank=True, null=True)),
                ('settlement_date', models.TextField(blank=True, null=True)),
                ('days_to_maturity', models.IntegerField(blank=True, null=True)),
                ('trade_tick_size', models.FloatField(blank=True, null=True)),
                ('settlement_tick_size', models.FloatField(blank=True, null=True)),
                ('spread_tick_size', models.FloatField(blank=True, null=True)),
                ('min_order_quantity', models.IntegerField(blank=True, null=True)),
                ('max_order_quantity', models.IntegerField(blank=True, null=True)),
                ('source', models.TextField()),
                ('ingested_at', models.TextField()),
            ],
            options={
                'verbose_name': 'Futures contract',
                'verbose_name_plural': 'Futures contracts',
                'db_table': 'futures_contract',
                'ordering': ['-listing_as_of', 'product_code', 'settlement_date', 'ticker'],
                'managed': False,
            },
        ),
    ]
