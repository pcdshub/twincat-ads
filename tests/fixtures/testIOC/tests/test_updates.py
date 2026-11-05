"""
Test that we get updating values back from the PLC.
"""
import time

import pytest

from .devices import PytmcVars


def test_basic_gets(vars_incr: PytmcVars):
    # It's not necessarily consistent when the new value is ready
    # Collect a few samples and make sure at least one pair has a change
    samples = [vars_incr.get()]
    for _ in range(5):
        time.sleep(1)
        samples.append(vars_incr.get())

    ok_list = []
    for s1, s2 in zip(samples[:-1], samples[1:]):
        ok = True
        for before_val, after_val in zip(s1, s2):
            if before_val == pytest.approx(after_val):
                ok = False
                break
        ok_list.append(ok)

    assert any(ok_list)
    assert not all(ok_list)
