# Generated by Django 5.1.4 on 2025-02-18 12:02

from django.db import migrations


class Migration(migrations.Migration):

    dependencies = [
        ('wiener', '0004_derivativeprice_confidence_level_and_more'),
    ]

    operations = [
        migrations.RemoveField(
            model_name='derivativeprice',
            name='confidence_level',
        ),
    ]
