"""
Test that we can write to the PLC and read back our writes.
"""
import time

import numpy as np
import pytest

from .devices import PytmcVars

setpoints = {
    "var_bool": (True, False, True, False),
    "var_byte": range(10),
    "var_sint": range(10),
    "var_usint": range(10),
    "var_word": range(10),
    "var_int": range(10),
    "var_uint": range(10),
    "var_enum": (0, 1, 2, "three", "four"),
    "var_dword": range(10),
    "var_dint": range(10),
    "var_udint": range(10),
    "var_real": (0.1, -42.3, 960.43),
    "var_lreal": (-24.5, 0.1, 101.2),
    "var_string": ("cats", "dogs", "pets"),
    "var_array": (np.array([n] * 100) for n in range(5)),
}

@pytest.mark.parametrize("attrname", list(setpoints))
def test_basic_puts(vars_set: PytmcVars, attrname: str):
    sig = getattr(vars_set, attrname)
    for putval in setpoints[attrname]:
        sig.put(putval)
        time.sleep(2)
        if isinstance(putval, str):
            getval = sig.get(as_string=True)
        else:
            getval = sig.get()
        if isinstance(putval, float):
            assert getval == pytest.approx(putval)
        elif isinstance(putval, np.ndarray):
            assert (getval == putval).all()
        else:
            assert getval == putval
