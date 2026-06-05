# testIOC/tests/unit/Makefile
TOP = ../../..
include $(TOP)/configure/CONFIG
-include RELEASE_UNIT.local

USR_CXXFLAGS += -DADS_UNIT_TEST
USR_CXXFLAGS += -std=c++14
USR_CXXFLAGS += -DTEST_DBD_PATH=\"$(TOP)/testIOC/tests/unit/dbd/testIoc.dbd\"
USR_CXXFLAGS += -DTEST_DB_PATH=\"$(TOP)/testIOC/DB/TestIOC.db\"
USR_DBDFLAGS += -I$(TOP)/dbd

USR_INCLUDES += -I$(TOP)/adsApp/src
USR_INCLUDES += -I$(TOP)/ADS/AdsLib
USR_INCLUDES += -I$(ASYN)/include


USR_LIBS += ads
USR_LIBS += asyn
USR_LIBS += dbRecStd
USR_LIBS += $(EPICS_BASE_IOC_LIBS)


INSTALL_DBD = $(TOP)/testIOC/tests/unit/dbd

DBD += testIoc.dbd
testIoc_DBD += base.dbd
testIoc_DBD += asyn.dbd
testIoc_DBD += drvAsynIPPort.dbd
testIoc_DBD += ads.dbd


GTESTPROD_HOST += test_basic
test_basic_SRCS += test_basic.cpp
GTESTS += test_basic

GTESTPROD_HOST += test_ioc_lifecycle
test_ioc_lifecycle_SRCS += test_ioc_lifecycle.cpp
test_ioc_lifecycle_SRCS += test_helpers.cpp
test_ioc_lifecycle_SRCS += testIoc_registerRecordDeviceDriver.cpp
GTESTS += test_ioc_lifecycle

# Use gtest compat rules — must be last
include $(TOP)/configure/RULES
-include $(GTEST)/cfg/compat.RULES_BUILD
