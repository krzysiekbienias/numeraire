# Generated manually — unmanaged Numeraire++ tables; state-only rename.

from django.db import migrations


class Migration(migrations.Migration):

    dependencies = [
        ('journal', '0003_par_curve_eod'),
    ]

    operations = [
        migrations.RenameField(
            model_name='tradelegexposureeod',
            old_name='pfe_97',
            new_name='pfe_975',
        ),
    ]
