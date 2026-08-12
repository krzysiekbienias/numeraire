# Generated manually for products_commodity book extension.
# Schema owned by sql/schema_v1.sql.

from django.db import migrations, models
import django.db.models.deletion


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0007_futures_contract'),
    ]

    operations = [
        migrations.CreateModel(
            name='ProductCommodity',
            fields=[
                (
                    'product',
                    models.OneToOneField(
                        on_delete=django.db.models.deletion.DO_NOTHING,
                        primary_key=True,
                        related_name='commodity',
                        serialize=False,
                        to='journal.product',
                    ),
                ),
                ('instrument_type', models.TextField()),
                ('product_code', models.TextField()),
                ('contract_ticker', models.TextField(blank=True, null=True)),
                ('contract_month', models.TextField(blank=True, null=True)),
                ('settlement_date', models.TextField(blank=True, null=True)),
                ('multiplier', models.FloatField(blank=True, null=True)),
                ('tick_size', models.FloatField(blank=True, null=True)),
                ('tick_value', models.FloatField(blank=True, null=True)),
                ('option_type', models.TextField(blank=True, null=True)),
                ('strike', models.FloatField(blank=True, null=True)),
                ('exercise_style', models.TextField(blank=True, null=True)),
                ('option_ticker', models.TextField(blank=True, null=True)),
                ('underlying_contract_ticker', models.TextField(blank=True, null=True)),
                ('structured_params', models.TextField(default='{}')),
            ],
            options={
                'verbose_name': 'Product (commodity)',
                'verbose_name_plural': 'Products (commodity)',
                'db_table': 'products_commodity',
                'managed': False,
            },
        ),
    ]
