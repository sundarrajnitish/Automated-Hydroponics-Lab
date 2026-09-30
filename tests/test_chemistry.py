import pytest

from hydrolab import chem


@pytest.mark.parametrize("ph", [4.5, 5.5, 6.0, 6.8, 7.5, 8.2])
def test_ph_and_alkalinity_are_inverse(ph):
    assert chem.ph(chem.alkalinity(ph)) == pytest.approx(ph, abs=1e-6)


def test_ph_rises_monotonically_with_alkalinity():
    alk = [-0.5 + 0.05 * i for i in range(120)]
    ph = [chem.ph(a) for a in alk]
    assert all(b > a for a, b in zip(ph, ph[1:]))


def test_buffer_capacity_peaks_near_pKa():
    beta = {p: chem.buffer_capacity(p) for p in (5.0, 6.0, 6.8, 7.6)}
    assert beta[6.8] > beta[6.0] > beta[5.0]


def test_growth_factor_plateau():
    assert chem.growth_factor(6.0) == pytest.approx(1.0)
    assert chem.growth_factor(5.6) == pytest.approx(1.0)
    assert chem.growth_factor(7.5) < 0.2
