import pytest

from bond_forge.src.bond_forge.bonds import FixedRateBond
from unittest.mock import MagicMock,patch


@pytest.fixture
def mock_bond():
    """
    Bond data fixture.
    -------
    return FixedRateBond
    """
    return FixedRateBond()


def test_create_bond_valid(mock_bond):
    """Test the create_bond method with valid inputs."""
    mock_bond.create_bond(
        face_value=2000,
        coupon_rate=0.07,
        coupon_frequency='semiannually',
        maturity_date='2040-01-01',
        issue_date='2021-01-01',
        is_callable=True,
        call_price=1050
    )

    # Verify that the attributes are correctly set
    assert mock_bond.face_value == 2000
    assert mock_bond.coupon_rate == 0.07
    assert mock_bond.coupon_frequency == 'semiannually'
    assert mock_bond.maturity_date == '2040-01-01'
    assert mock_bond.issue_date == '2021-01-01'
    assert mock_bond.is_callable == True
    assert mock_bond.call_price == 1050

def test_create_bond_non_callable(mock_bond):
    """Test the create_bond method when the bond is non-callable."""
    mock_bond.create_bond(
        face_value=1000,
        coupon_rate=0.05,
        coupon_frequency='quarterly',
        maturity_date='2030-01-01',
        issue_date='2020-01-01',
        is_callable=False,
        call_price=1100  # Should be ignored
    )

    # Verify that attributes are correctly set and call_price is None
    assert mock_bond.face_value == 1000
    assert mock_bond.coupon_rate == 0.05
    assert mock_bond.coupon_frequency == 'quarterly'
    assert mock_bond.maturity_date == '2030-01-01'
    assert mock_bond.issue_date == '2020-01-01'
    assert mock_bond.is_callable is False
    assert mock_bond.call_price is None

def test_periodic_coupon(mock_bond):
    result = mock_bond.periodic_coupon()
    assert result == 12.5


@patch("bond_forge.src.bond_forge.bonds.QuantLibToolKit")
def test_create_payment_schedule(mock_toolkit_class, mock_bond):
    """Test the create_payment_schedule method."""
    # Create a mock instance of QuantLibToolKit
    mock_toolkit_instance = mock_toolkit_class.return_value
    mock_toolkit_instance.define_schedule.return_value = "Mocked Schedule"

    # Call the method
    result = mock_bond.create_payment_schedule()

    # Verify the mock was called with the correct arguments
    mock_toolkit_instance.define_schedule.assert_called_once_with(
        valuation_date="2020-01-01",  # bond.issue_date
        termination_date="2030-01-01",  # bond.maturity_date
        freq_period="quarterly"
    )

    # Assert the return value matches the mocked schedule
    assert result == "Mocked Schedule"

def test_create_payment_schedule_invalid_dates(mock_bond):
    """Test create_payment_schedule with invalid dates."""
    mock_bond.issue_date = "2030-01-01"  # Invalid: issue_date > maturity_date
    mock_bond.maturity_date = "2020-01-01"

    with pytest.raises(ValueError, match="Issue date must be earlier than maturity date"):
        mock_bond.create_payment_schedule()