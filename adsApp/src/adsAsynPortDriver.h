#ifndef ADSASYNPORTDRIVER_H_
#define ADSASYNPORTDRIVER_H_

#include "AdsLib.h"
#include "adsAsynPortDriverUtils.h"
#include "asynPortDriver.h"
#include <dbBase.h>
#include <dbCommon.h>
#include <dbStaticLib.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

/** Class derived of asynPortDriver for ads communication with TwinCAT plc:s */

class adsAsynPortDriver : public asynPortDriver
{
public:
  adsAsynPortDriver(const char *portName,
                    const char *ipaddr,
                    const char *amsaddr,
                    unsigned int amsport,
                    int paramTableSize,
                    unsigned int priority,
                    int autoConnect,
                    int defaultSampleTimeMS,
                    int maxDelayTimeMS,
                    int adsTimeoutMS,
                    ADSTIMESOURCE defaultTimeSource);

  virtual ~adsAsynPortDriver();
  virtual void report(FILE *fp, int details);
  virtual asynStatus disconnect(asynUser *pasynUser);
  virtual asynStatus connect(asynUser *pasynUser);
  virtual asynStatus drvUserCreate(asynUser *pasynUser,
                                   const char *drvInfo,
                                   const char **pptypeName,
                                   size_t *psize);
  virtual asynStatus writeOctet(asynUser *pasynUser,
                                const char *value,
                                size_t maxChars,
                                size_t *nActual);
  virtual asynStatus readOctet(asynUser *pasynUser,
                               char *value,
                               size_t maxChars,
                               size_t *nActual,
                               int *eomReason);
  virtual asynStatus writeInt32(asynUser *pasynUser,
                                epicsInt32 value);
  virtual asynStatus writeFloat64(asynUser *pasynUser,
                                  epicsFloat64 value);
  virtual asynStatus readInt8Array(asynUser *pasynUser,
                                   epicsInt8 *value,
                                   size_t nElements,
                                   size_t *nIn);
  virtual asynStatus writeInt8Array(asynUser *pasynUser,
                                    epicsInt8 *value,
                                    size_t nElements);
  virtual asynStatus readInt16Array(asynUser *pasynUser,
                                    epicsInt16 *value,
                                    size_t nElements,
                                    size_t *nIn);
  virtual asynStatus writeInt16Array(asynUser *pasynUser,
                                     epicsInt16 *value,
                                     size_t nElements);
  virtual asynStatus readInt32Array(asynUser *pasynUser,
                                    epicsInt32 *value,
                                    size_t nElements,
                                    size_t *nIn);
  virtual asynStatus writeInt32Array(asynUser *pasynUser,
                                     epicsInt32 *value,
                                     size_t nElements);
  virtual asynStatus readFloat32Array(asynUser *pasynUser,
                                      epicsFloat32 *value,
                                      size_t nElements,
                                      size_t *nIn);
  virtual asynStatus writeFloat32Array(asynUser *pasynUser,
                                       epicsFloat32 *value,
                                       size_t nElements);
  virtual asynStatus readFloat64Array(asynUser *pasynUser,
                                      epicsFloat64 *value,
                                      size_t nElements,
                                      size_t *nIn);
  virtual asynStatus writeFloat64Array(asynUser *pasynUser,
                                       epicsFloat64 *value,
                                       size_t nElements);
  // 64-bit integer interface support (asynInt64 and asynInt64Array)
  virtual asynStatus writeInt64(asynUser *pasynUser,
                                epicsInt64 value);
  virtual asynStatus readInt64Array(asynUser *pasynUser,
                                    epicsInt64 *value,
                                    size_t nElements,
                                    size_t *nIn);
  virtual asynStatus writeInt64Array(asynUser *pasynUser,
                                     epicsInt64 *value,
                                     size_t nElements);

  asynStatus adsUpdateParameterLock(adsParamInfo &paramInfo,
                                    const void *data, bool callCallbacks = true);
  asynStatus invalidateParamsLock(uint16_t amsPort);
  asynStatus refreshParamsLock(long adsClientPort, uint16_t amsPort);
  asynStatus adsDelRoute();
  asynStatus adsAddRoute();
  asynStatus fireAllCallbacksLock();
  asynUser *getTraceAsynUser();
  int getParamTableSize();
  adsParamInfo *getAdsParamInfo(int index);
  int getAdsParamCount();
  bool isCallbackAllowed(adsParamInfo &paramInfo);
  bool isCallbackAllowed(uint16_t amsPort);
  bool okToProcessBulkReads() const;
  bool setOkToProcessBulkReads(bool ok);
  void emplaceInDataCallbackQueue(adsParamInfo &paramInfo, const AdsNotificationHeader *pNotification);

  long getAdsClientPortNumberForThreadId(epicsThreadId threadId);
  long addAdsClientPortNumberForThreadId(epicsThreadId threadId);
  asynStatus delAdsClientPortNumberForThreadId(epicsThreadId threadId);

  void cyclicThread();
  void bulkReadThread();
  void dataCallbackThread();
  void poll_info(char *name);

protected:
private:
  // Asyn and EPICS methods
  asynStatus connectLock(asynUser *pasynUser);
  asynStatus disconnectLock(asynUser *pasynUser);

  asynStatus validateDrvInfo(const char *drvInfo);
  asynStatus getRecordInfoFromDrvInfo(const char *drvInfo,
                                      adsParamInfo &paramInfo);
  asynStatus parsePlcInfofromDrvInfo(const char *drvInfo,
                                     adsParamInfo &paramInfo);
  asynStatus refreshParams(long adsClientPort);
  asynStatus refreshParams(long adsClientPort, uint16_t amsPort);
  asynStatus invalidateParams(uint16_t amsPort);
  asynStatus adsUpdateParameter(adsParamInfo &paramInfo,
                                const void *data, bool callCallbacks = true);
  asynStatus adsUpdateParameter(adsParamInfo &paramInfo,
                                const void *data, size_t dataSize, bool callCallbacks = true);
  asynStatus adsUpdateParameterLock(adsParamInfo &paramInfo,
                                    const void *data,
                                    size_t dataSize, bool callCallbacks = true);

  // ADS methods
  asynStatus adsAddDataCallback(long adsClientPort, adsParamInfo &paramInfo);

  asynStatus adsDelDataCallback(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus adsDelDataCallback(long adsClientPort, adsParamInfo &paramInfo,
                                bool blockErrorMsg);
  asynStatus adsAddSymbolsChangedCallback(long adsClientPort, amsPortInfo &port);
  asynStatus adsDelSymbolsChangedCallback(long adsClientPort, amsPortInfo &port);
  asynStatus adsGetSymInfoByName(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus adsGetSymInfoByName(long adsClientPort, uint16_t amsPort,
                                 const char *varName,
                                 adsSymbolEntry &info);
  asynStatus adsGetSymInfoByName(long adsClientPort, uint16_t amsPort,
                                 const char *varName,
                                 adsSymbolEntry &info,
                                 long *errorCode);
  asynStatus adsGetSymHandleByName(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus adsGetSymHandleByName(long adsClientPort, adsParamInfo &paramInfo,
                                   bool blockErrorMsg);
  asynStatus adsReleaseSymbolicHandle(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus adsReleaseSymbolicHandle(long adsClientPort, adsParamInfo &paramInfo,
                                      bool blockErrorMsg);
  asynStatus adsConnect(long adsClientPort);
  asynStatus adsDisconnect(long adsClientPort);
  asynStatus adsWriteParam(long adsClientPort, adsParamInfo &paramInfo,
                           const void *binaryBuffer,
                           uint32_t bytesToWrite);
  asynStatus adsReadParam(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus adsReadParam(long adsClientPort, adsParamInfo &paramInfo,
                          long &error,
                          int updateAsynPar);
  asynStatus adsReadState(long adsClientPort, uint16_t &adsState);
  asynStatus adsReadStateLock(long adsClientPort, uint16_t amsport,
                              uint16_t &adsState,
                              bool blockErrorMsg);
  asynStatus adsReadStateLock(long adsClientPort, uint16_t amsport,
                              uint16_t &adsState,
                              bool blockErrorMsg,
                              long &error);
  asynStatus adsReadState(long adsClientPort,
                          uint16_t amsport,
                          uint16_t &adsState,
                          bool blockErrorMsg,
                          long &error);
  asynStatus adsWriteState(long adsClientPort, uint16_t amsport,
                           uint16_t adsState);

  asynStatus adsGenericArrayWrite(asynUser *pasynUser,
                                  long allowedType,
                                  const void *epicsDataBuffer,
                                  size_t nEpicsBufferBytes);
  asynStatus adsGenericArrayRead(asynUser *pasynUser,
                                 long allowedType,
                                 void *epicsDataBuffer,
                                 size_t nEpicsBufferBytes,
                                 size_t *nBytesRead);
  asynStatus adsReadVersion(long adsClientPort, amsPortInfo &port);
  asynStatus updateParamInfoWithPLCInfo(long adsClientPort, adsParamInfo &paramInfo);
  asynStatus refreshParamTime(adsParamInfo &paramInfo);
  asynStatus setAlarmPortLock(uint16_t amsPort, int alarm, int severity);
  asynStatus setAlarmPort(uint16_t amsPort, int alarm, int severity);
  asynStatus setAlarmParam(adsParamInfo &paramInfo, int alarm, int severity);
  asynStatus fireCallbacks(adsParamInfo &paramInfo);
  asynStatus addNewAmsPortToList(uint16_t amsPort);
  asynStatus getAmsPortObject(uint16_t amsPort, int &index);
  asynStatus adsAddToBulkRead(adsParamInfo &paramInfo);
  int adsGetBulkTimeStamp(uint16_t amsPort);

  // Octet interface methods (ascii command parser through readoctet() and writeoctet())
  int octetCMDreadIt(long adsClientPort, char *outbuf,
                     size_t outlen);
  int octetCMDwriteIt(long adsClientPort, const char *inbuf,
                      size_t inlen);
  int octetCmdHandleInputLine(long adsClientPort, const char *input_line,
                              adsOctetOutputBufferType *buffer);
  int octetMotorHandleOneArg(long adsClientPort, const char *myarg_1,
                             adsOctetOutputBufferType *buffer);
  int octetMotorHandleADRCmd(long adsClientPort, const char *arg,
                             uint16_t adsport,
                             adsOctetOutputBufferType *buffer);
  int octetAdsReadByName(long adsClientPort, uint16_t amsPort,
                         const char *variableAddr,
                         adsOctetOutputBufferType *outBuffer);
  int octetAdsWriteByName(long adsClientPort, uint16_t amsPort,
                          const char *variableAddr,
                          const char *asciiValueToWrite,
                          adsOctetOutputBufferType *outBuffer);
  int octetAdsReadByGroupOffset(long adsClientPort, uint16_t amsPort,
                                adsSymbolEntry *info,
                                adsOctetOutputBufferType *outBuffer);
  int octetAdsWriteByGroupOffset(long adsClientPort, uint16_t amsPort,
                                 uint32_t group,
                                 uint32_t offset,
                                 uint16_t dataType,
                                 uint32_t dataSize,
                                 const char *asciiValueToWrite,
                                 adsOctetOutputBufferType *asciiResponseBuffer);

  std::string ipaddr_;
  std::string amsaddr_;
  int autoConnect_;
  int defaultSampleTime_ms_;
  int defaultMaxDelayTime_ms_;
  uint32_t adsTimeout_ms_;
  std::unordered_map<epicsThreadId, AmsClientPortEntry> threadIdToAmsClientPortMap_;
  double cyclicThreadCycleTime_s_;
  bool routeEstablished_;
  std::mutex adsAddDelRouteMutex_;
  uint16_t amsportDefault_;
  unsigned int priority_;
  AmsNetId remoteNetId_;
  std::string amsNetIdStr_;
  std::vector<adsParamInfo> adsParamArray_;
  std::vector<amsPortInfo> amsPortList_;
  ADSTIMESOURCE defaultTimeSource_;
  std::recursive_mutex threadIdToAmsClientPortMapMutex_;
  std::mutex callbacksMutex_;

  // octet
  adsOctetOutputBufferType octetAsciiBuffer_;
  uint8_t octetBinaryBuffer_[ADS_CMD_BUFFER_SIZE];
  int octetReturnVarName_;

  // bulk read
  size_t bulkReadMaxTsEntrySize_;
  size_t MaxNumberOfSubCallsPerBulkRead_;
  std::vector<tsentry> bulkTs_;
  std::vector<BulkReadInfo> bulkReadInfo_;
  int bulkReadTimeDelay_us_;   // Rate to process bulk reads
  int bulkReadTimeElapsed_us_; // Time of last bulk read loop
  bool okToProcessBulkReads_;  // OK to process bulk reads
  std::recursive_mutex adsBulkInfoUpdateMutex_;

  // data callback thread
  struct datacbinfo
  {
    datacbinfo(adsParamInfo &paramInfo, const AdsNotificationHeader *pNotification);
    adsParamInfo &paramInfo;
    std::vector<uint8_t> data;
    const AdsNotificationHeader notification;
  };
  std::queue<datacbinfo> datacbqueue;
  std::mutex dataCbQueueMutex_;
};

#define MAXCBQSIZE 10000
#define SEC_TO_UNIX_EPOCH 11644473600LL

class AdsClientPortGuard
{
public:
  AdsClientPortGuard(adsAsynPortDriver &adsAsynPortDriver);
  ~AdsClientPortGuard();
  long getAdsClientPort() const;

private:
  adsAsynPortDriver &adsAsynPortDriver_;
  long adsClientPort_;
  epicsThreadId threadId_;
};

#endif /* ADSASYNPORTDRIVER_H_ */
