# Generated manually for futures_daily_eod (schema owned by sql/schema_v1.sql).

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0004_rename_pfe_97_to_pfe_975'),
    ]

    operations = [
        migrations.CreateModel(
            name='FuturesDailyEod',
            fields=[
                ('id', models.AutoField(primary_key=True, serialize=False)),
                ('ticker', models.TextField()),
                ('product_code', models.TextField(blank=True, null=True)),
                ('as_of', models.DateField()),
                ('session_calendar', models.TextField()),
                ('open', models.FloatField()),
                ('high', models.FloatField()),
                ('low', models.FloatField()),
                ('close', models.FloatField()),
                ('settlement_price', models.FloatField(blank=True, null=True)),
                ('currency', models.TextField()),
                ('volume', models.FloatField(blank=True, null=True)),
                ('dollar_volume', models.FloatField(blank=True, null=True)),
                ('vwap', models.FloatField(blank=True, null=True)),
                ('trade_count', models.IntegerField(blank=True, null=True)),
                ('source', models.TextField()),
                ('timespan', models.TextField()),
                ('provider_timestamp_utc_ms', models.BigIntegerField(blank=True, null=True)),
                ('ingested_at', models.TextField()),
            ],
            options={
                'verbose_name': 'Futures daily EOD',
                'verbose_name_plural': 'Futures daily EOD',
                'db_table': 'futures_daily_eod',
                'ordering': ['-as_of', 'ticker'],
                'managed': False,
            },
        ),
    ]
