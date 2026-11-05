"""
Test that we can move motors.
"""
import pytest

from .devices import AllPVs


@pytest.mark.parametrize("mot", ("m1", "m2"))
def test_basic_motion(all_pvs: AllPVs, mot: str):
    mot = getattr(all_pvs, mot)
    mot.velocity.put(10)
    goal = mot.position + 4
    mot.move(goal, wait=True, timeout=5.0)
    assert mot.position == pytest.approx(goal)
