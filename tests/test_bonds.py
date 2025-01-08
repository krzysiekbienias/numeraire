import pytest

from bond_forge.src.bond_forge.bonds import FixedRateBond


@pytest.fixture
def bond():
    """
    Bond data fixture.
    Returns
    -------
    return
    """
    return FixedRateBond()


def test_periodic_coupon(bond):
    result = bond.periodic_coupon()
    assert result == 100
