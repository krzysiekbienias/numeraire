# Generated by Django 5.1.4 on 2025-02-18 11:38

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('wiener', '0002_tradebook_option_style'),
    ]

    operations = [
        migrations.AddField(
            model_name='tradebook',
            name='structured_params',
            field=models.JSONField(blank=True, null=True),
        ),
    ]
