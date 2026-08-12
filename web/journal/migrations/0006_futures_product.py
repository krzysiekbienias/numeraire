# Generated manually for futures_product catalog mirror.
# Schema owned by sql/schema_v1.sql (+ ApplySchemaPatches for existing DBs).
# Do not AddField UniverseInstrument here — that model is not in migration state
# (managed=False mirror only; flags live in schema_v1 / ApplySchemaPatches).

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0005_futures_daily_eod'),
    ]

    operations = [
        migrations.CreateModel(
            name='FuturesProduct',
            fields=[
                ('product_code', models.TextField(primary_key=True, serialize=False)),
                ('name', models.TextField(blank=True, null=True)),
                ('asset_class', models.TextField(blank=True, null=True)),
                ('asset_sub_class', models.TextField(blank=True, null=True)),
                ('sector', models.TextField(blank=True, null=True)),
                ('sub_sector', models.TextField(blank=True, null=True)),
                ('trading_venue', models.TextField(blank=True, null=True)),
                ('type', models.TextField(blank=True, null=True)),
                ('trade_currency_code', models.TextField(blank=True, null=True)),
                ('settlement_currency_code', models.TextField(blank=True, null=True)),
                ('settlement_method', models.TextField(blank=True, null=True)),
                ('settlement_type', models.TextField(blank=True, null=True)),
                ('price_quotation', models.TextField(blank=True, null=True)),
                ('unit_of_measure', models.TextField(blank=True, null=True)),
                ('unit_of_measure_qty', models.FloatField(blank=True, null=True)),
                ('as_of', models.TextField(blank=True, null=True)),
                ('last_updated', models.TextField(blank=True, null=True)),
                ('source', models.TextField()),
                ('ingested_at', models.TextField()),
            ],
            options={
                'verbose_name': 'Futures product',
                'verbose_name_plural': 'Futures products',
                'db_table': 'futures_product',
                'ordering': ['product_code'],
                'managed': False,
            },
        ),
    ]
