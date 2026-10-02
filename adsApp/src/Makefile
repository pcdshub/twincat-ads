TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CXXFLAGS += -std=c++14 -DCONFIG_DEFAULT_LOGLEVEL=1
USR_CPPFLAGS += -I ../../../ADS/AdsLib/
USR_CPPFLAGS += -I $(TOP)/adsApp/src/toml11/single_include
USR_LDFLAGS  += -lpthread

#=============================
# Build the IOC application

LIBRARY_IOC = ads

DBD += ads.dbd

# Add source files
ADSSUPPORTSOURCES = \
  AdsDef.cpp \
  AdsLib.cpp \
  AmsConnection.cpp \
  AmsNetId.cpp \
  AmsPort.cpp \
  AmsRouter.cpp \
  AdsLibDiscovery.cpp \
  Log.cpp \
  NotificationDispatcher.cpp \
  Sockets.cpp \
  Frame.cpp


ads_SRCS += adsAsynPortDriver.cpp
ads_SRCS += adsAsynPortDriverUtils.cpp
ads_SRCS += adsIocConfig.cpp
ads_SRCS += ${ADSSUPPORTSOURCES}

ads_LIBS += asyn
ads_LIBS += $(EPICS_BASE_IOC_LIBS)


#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
