# testIOC/tests/unit/Makefile
TOP = ../../..
include $(TOP)/configure/CONFIG

USR_CXXFLAGS += -DADS_UNIT_TEST
USR_CXXFLAGS += -std=c++14

USR_INCLUDES += -I$(TOP)/adsApp/src
USR_INCLUDES += -I$(TOP)/ADS/AdsLib
USR_INCLUDES += -I$(ASYN)/include

USR_LIBS += ads
USR_LIBS += asyn
USR_LIBS += $(EPICS_BASE_IOC_LIBS)

GTESTPROD_HOST += test_basic
test_basic_SRCS += test_basic.cpp
GTESTS += test_basic

# Use gtest compat rules — must be last
include $(TOP)/configure/RULES
-include $(GTEST)/cfg/compat.RULES_BUILD
