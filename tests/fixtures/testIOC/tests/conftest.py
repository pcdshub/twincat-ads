from __future__ import annotations

import pytest

from .devices import AllPVs


@pytest.fixture(scope="session")
def all_pvs():
    pvs = AllPVs()
    pvs.wait_for_connection()
    return pvs


@pytest.fixture(scope="session")
def vars_incr(all_pvs: AllPVs):
    return all_pvs.incr


@pytest.fixture(scope="session")
def vars_set(all_pvs: AllPVs):
    return all_pvs.setp
