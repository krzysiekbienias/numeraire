"""Routing between Django's own database and the C++ batch database.

Models of the `journal` app mirror tables created by `sql/schema_v1.sql` and are
declared `managed = False`. They must never be migrated: the C++ side
(`BootstrapTradeDatabaseSchema`) owns that schema.

Booking new trades stays outside the ORM - `scripts/import_trade_bundle.py` holds
the domain validation and is the only supported writer for the catalog tables.
"""


class NumeraireRouter:
    """Send `journal` models to the `numeraire` database, everything else to `default`."""

    app_label = 'journal'
    db_alias = 'numeraire'

    def db_for_read(self, model, **hints):
        if model._meta.app_label == self.app_label:
            return self.db_alias
        return None

    def db_for_write(self, model, **hints):
        if model._meta.app_label == self.app_label:
            return self.db_alias
        return None

    def allow_relation(self, obj1, obj2, **hints):
        labels = {obj1._meta.app_label, obj2._meta.app_label}
        if labels == {self.app_label}:
            return True
        if self.app_label in labels:
            return False
        return None

    def allow_migrate(self, db, app_label, model_name=None, **hints):
        if app_label == self.app_label:
            return False
        return db == 'default'
