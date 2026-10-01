# Schema owned by sql/schema_v1.sql (+ ApplySchemaPatches for existing DBs).
# State-only: journal models are managed=False and never ALTER the Numeraire DB.

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0008_products_commodity'),
    ]

    operations = [
        migrations.AddField(
            model_name='tradeleg',
            name='trade_price',
            field=models.FloatField(blank=True, null=True),
        ),
    ]
