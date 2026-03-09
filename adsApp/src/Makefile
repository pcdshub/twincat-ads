TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CXXFLAGS += -std=c++11
USR_CPPFLAGS += -I ../../../ADS/AdsLib/
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
  AmsPort.cpp \
  AmsRouter.cpp \
  Log.cpp \
  NotificationDispatcher.cpp \
  Sockets.cpp \
  Frame.cpp


ads_SRCS += adsAsynPortDriver.cpp
ads_SRCS += adsAsynPortDriverUtils.cpp
ads_SRCS += AdsDatatypeEntry.cpp
ads_SRCS += AdsDatatypeIndex.cpp
ads_SRCS += AdsSymbolIndex.cpp
ads_SRCS += AdsSymbolParser.cpp
ads_SRCS += AdsSymbolUploadInfo2.cpp
ads_SRCS += ${ADSSUPPORTSOURCES}

ads_LIBS += asyn
ads_LIBS += $(EPICS_BASE_IOC_LIBS)


#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


