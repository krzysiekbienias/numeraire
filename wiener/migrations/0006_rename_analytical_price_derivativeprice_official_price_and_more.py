# Generated by Django 5.1.4 on 2025-03-10 11:50

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('wiener', '0005_remove_derivativeprice_confidence_level'),
    ]

    operations = [
        migrations.RenameField(
            model_name='derivativeprice',
            old_name='analytical_price',
            new_name='official_price',
        ),
        migrations.RenameField(
            model_name='derivativeprice',
            old_name='pricing_model',
            new_name='pricing_framework',
        ),
        migrations.RemoveField(
            model_name='derivativeprice',
            name='extra_price',
        ),
        migrations.RemoveField(
            model_name='derivativeprice',
            name='market_price',
        ),
        migrations.AddField(
            model_name='derivativeprice',
            name='pricing_method',
            field=models.CharField(blank=True, max_length=50, null=True),
        ),
    ]
