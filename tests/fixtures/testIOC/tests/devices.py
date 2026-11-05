from __future__ import annotations

from ophyd.device import Component as Cpt
from ophyd.device import Device
from ophyd.device import FormattedComponent as FCpt
from pcdsdevices.epics_motor import BeckhoffAxis
from pcdsdevices.signal import PytmcSignal

cpt_kw = {"io": "{io}", "add_prefix": ("suffix", "io")}


class PytmcVars(Device):
    """
    Ophyd device corresponding to ST_PytmcTypes in test IOC
    """
    var_bool = FCpt(PytmcSignal, "{prefix}:BOOL", **cpt_kw)
    var_byte = FCpt(PytmcSignal, "{prefix}:BYTE", **cpt_kw)
    var_sint = FCpt(PytmcSignal, "{prefix}:SINT", **cpt_kw)
    var_usint = FCpt(PytmcSignal, "{prefix}:USINT", **cpt_kw)
    var_word = FCpt(PytmcSignal, "{prefix}:WORD", **cpt_kw)
    var_int = FCpt(PytmcSignal, "{prefix}:INT", **cpt_kw)
    var_uint = FCpt(PytmcSignal, "{prefix}:UINT", **cpt_kw)
    var_enum = FCpt(PytmcSignal, "{prefix}:ENUM", **cpt_kw)
    var_dword = FCpt(PytmcSignal, "{prefix}:DWORD", **cpt_kw)
    var_dint = FCpt(PytmcSignal, "{prefix}:DINT", **cpt_kw)
    var_udint = FCpt(PytmcSignal, "{prefix}:UDINT", **cpt_kw)
    var_real = FCpt(PytmcSignal, "{prefix}:REAL", **cpt_kw)
    var_lreal = FCpt(PytmcSignal, "{prefix}:LREAL", **cpt_kw)
    var_string = FCpt(PytmcSignal, "{prefix}:STRING", string=True, **cpt_kw)
    var_array = FCpt(PytmcSignal, "{prefix}:ARRAY", **cpt_kw)

    def __init__(self, prefix: str, *, io: str, **kwargs):
        self.io = io
        super().__init__(prefix, **kwargs)


class AllPVs(Device):
    m1 = Cpt(BeckhoffAxis, ":M1")
    m2 = Cpt(BeckhoffAxis, ":M2")
    incr = Cpt(PytmcVars, ":INCR", io="i")
    setp = Cpt(PytmcVars, ":SET", io="io")

    def __init__(self, prefix: str = "PLC:TST:IOC", name="all_test_pvs"):
        super().__init__(prefix, name=name)
