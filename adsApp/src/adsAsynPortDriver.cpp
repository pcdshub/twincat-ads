// #define MCB_DEBUG
/*
 * adsAsynPortDriver.cpp
 *
 * Class derived of asynPortDriver for ADS communication with TwinCAT plcs.
 * AdsLib written by Beckhoff is used for communication: https://github.com/Beckhoff/ADS
 *
 * Author: Anders Sandström
 * Edited to add bulk reads: Michael Browne
 *
 * Created January 25, 2018
 * Edited  December 6, 2019
 */

#define USE_TYPED_RSET // Shut up about rset already!
#include "adsAsynPortDriver.h"

#include <errno.h>
#include <math.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <callback.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <epicsTypes.h>
#include <initHooks.h>
#include <iocsh.h>

#include <alarm.h>
#include <dbAccess.h>
#include <dbStaticLib.h>
#include <epicsExport.h>

static const char *driverName = "adsAsynPortDriver";
static std::unique_ptr<adsAsynPortDriver> adsAsynPortObj;
static long oldTimeStamp = 0;
static struct timeval oldTime = {0};
static int allowCallbackEpicsState = 0;
static initHookState currentEpicsState = initHookAtIocBuild;

/** Callback hook for EPICS state.
 * \param[in] state EPICS state
 * \return void
 * Will be called be the EPICS framework with the current EPICS state as it changes.
 */
static void getEpicsState(initHookState state)
{
  static struct timeval start;
  struct timeval now, diff;

  if (!adsAsynPortObj)
  {
    printf("%s:%s: ERROR: adsAsynPortObj==NULL\n", driverName, __func__);
    return;
  }

  asynUser *asynTraceUser = adsAsynPortObj->getTraceAsynUser();

  switch (state)
  {
  case initHookAtIocBuild: /* Start of iocBuild/iocInit commands */
    break;
  case initHookAtBeginning:
    break;
  case initHookAfterCallbackInit:
    break;
  case initHookAfterCaLinkInit:
    break;
  case initHookAfterInitDrvSup:
    break;
  case initHookAfterInitRecSup:
    break;
  case initHookAfterInitDevSup:
    gettimeofday(&start, NULL);
    break;
  case initHookAfterInitDatabase:
    gettimeofday(&now, NULL);
    timersub(&now, &start, &diff);
    printf("Database initialization took %ld.%05ld seconds.\n", diff.tv_sec, diff.tv_usec);
    break;
  case initHookAfterFinishDevSup:
    break;
  case initHookAfterScanInit:
    break;
  case initHookAfterInitialProcess:
    break;
  case initHookAfterCaServerInit:
    break;
  case initHookAfterIocBuilt: /* End of iocBuild command */
    break;
  case initHookAtIocRun: /* Start of iocRun command */
    break;
  case initHookAfterDatabaseRunning:
    break;
  case initHookAfterCaServerRunning:
    break;
  case initHookAfterIocRunning: /* End of iocRun/iocInit commands */
    allowCallbackEpicsState = 1;
    if (!adsAsynPortObj)
    {
      printf("%s:%s: ERROR: adsAsynPortObj==NULL\n", driverName, __func__);
      return;
    }
    adsAsynPortObj->setOkToProcessBulkReads(true);
    printf("Begin polling PLC\n");
    break;
  case initHookAtIocPause: /* Start of iocPause command */
    break;
  case initHookAfterCaServerPaused:
    break;
  case initHookAfterDatabasePaused:
    break;
  case initHookAfterIocPaused: /* End of iocPause command */
    break;
  default:
    break;
  }

  currentEpicsState = state;
  asynPrint(asynTraceUser, ASYN_TRACEIO_DRIVER,
            "%s:%s: EPICS state: %s (%d). Allow ADS callbacks: %s.\n",
            driverName, __func__, epicsStateToString((int)state), (int)state, allowCallbackEpicsState ? "true" : "false");
}

/** Register EPICS hook function
 * \return void
 */
int initHook(void)
{
  return (initHookRegister(getEpicsState));
}

/** Callback from ads lib for symbols changed in PLC.
 * \param[in] pAddr AmsAddr of the system generating the callback.
 * \param[in] pNotification Data structure containing the updated data and timestamp information.
 * \param[in] hUser Identification index of the callback parameter.
 * \return void
 * This function will be called by the ADS lib if the symbol version in the PLC is changed.
 */
static void adsSymbolsChangedCallback(const AmsAddr *pAddr, const AdsNotificationHeader *pNotification, uint32_t hUser)
{
  if (!adsAsynPortObj)
  {
    printf("%s:%s: ERROR: adsAsynPortObj==NULL\n", driverName, __func__);
    return;
  }

  asynUser *asynTraceUser = adsAsynPortObj->getTraceAsynUser();

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*adsAsynPortObj, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  asynPrint(asynTraceUser, ASYN_TRACE_INFO, "%s:%s: Symbols changed for Ams-port %u.\n", driverName, __func__, pAddr->port);

  adsAsynPortObj->invalidateParamsLock(pAddr->port);
  adsAsynPortObj->refreshParamsLock(adsClientPort, pAddr->port);
}

/** Callback from ads lib for updated data.
 * \param[in] pAddr AmsAddr of the system generating the callback.
 * \param[in] pNotification Data structure containing the updated data and timestamp information.
 * \param[in] hUser Identification index of the callback parameter.
 * \return void
 * This function will be called by the ADS lib when a registered parameter is updated (changed in PLC).
 */
static void adsDataCallback(const AmsAddr *pAddr, const AdsNotificationHeader *pNotification, uint32_t hUser)
{
  if (!adsAsynPortObj)
  {
    printf("%s:%s: ERROR: adsAsynPortObj==NULL\n", driverName, __func__);
    return;
  }

  asynUser *asynTraceUser = adsAsynPortObj->getTraceAsynUser();
  asynPrint(asynTraceUser, ASYN_TRACE_FLOW | ASYN_TRACEIO_DRIVER, "%s:%s:\n", driverName, __func__);

  struct timeval newTime;
  gettimeofday(&newTime, NULL);

  asynPrint(asynTraceUser, ASYN_TRACEIO_DRIVER, "TIME %ld.%06ld\n", (long)newTime.tv_sec, (long)newTime.tv_usec);

  long secs_used = (newTime.tv_sec - oldTime.tv_sec); // avoid overflow by subtracting first
  long micros_used = ((secs_used * 1000000) + newTime.tv_usec) - (oldTime.tv_usec);
  oldTime = newTime;

  // Ensure hUser is within range
  if (hUser > (uint32_t)(adsAsynPortObj->getParamTableSize() - 1) || hUser < 0)
  {
    asynPrint(asynTraceUser, ASYN_TRACE_ERROR, "%s:%s: hUser out of range: %u.\n", driverName, __func__, hUser);
    return;
  }

  // Get paramInfo
  auto paramInfo = adsAsynPortObj->getAdsParamInfo(hUser);
  if (!paramInfo)
  {
    asynPrint(asynTraceUser, ASYN_TRACE_ERROR, "%s:%s: getAdsParamInfo() for hUser %u failed\n", driverName, __func__, hUser);
    return;
  }

  asynPrint(asynTraceUser, ASYN_TRACEIO_DRIVER, "Process callback for parameter %s (%d).\n", paramInfo->drvInfo.c_str(), paramInfo->paramIndex);
  asynPrint(asynTraceUser, ASYN_TRACEIO_DRIVER, "hUser 0x%x, data size[b]: %d.\n", hUser, pNotification->cbSampleSize);
  asynPrint(asynTraceUser, ASYN_TRACEIO_DRIVER, "time stamp [100ns]: %ld, since last plc [ms]: %4.2lf, since last ioc [ms]: %4.2lf.\n",
            pNotification->nTimeStamp, ((double)(pNotification->nTimeStamp - oldTimeStamp)) / 10000.0, (((double)(micros_used)) / 1000.0));
  oldTimeStamp = pNotification->nTimeStamp;

  // Ensure hUser is equal to parameter index
  if ((int)hUser != paramInfo->paramIndex)
  {
    asynPrint(asynTraceUser, ASYN_TRACE_ERROR, "%s:%s: hUser not equal to parameter index (%u vs %d).\n", driverName, __func__, hUser, paramInfo->paramIndex);
    return;
  }

  adsAsynPortObj->emplaceInDataCallbackQueue(*paramInfo, pNotification);
}

/** Start cyclic thread for supervision of connection.
 * \param[in] drvPvt adsAsynPortDriver object
 * \return void
 */
void cyclicThread(void *drvPvt)
{
  adsAsynPortDriver *pPvt = (adsAsynPortDriver *)drvPvt;
  pPvt->cyclicThread();
}

/** Start bulk read thread.
 * \param[in] drvPvt adsAsynPortDriver object
 * \return void
 */
void bulkReadThread(void *drvPvt)
{
  adsAsynPortDriver *pPvt = (adsAsynPortDriver *)drvPvt;
  pPvt->bulkReadThread();
}

/** Start data callback thread
 * \param[in] drvPvt adsAsynPortDriver object
 * \return void
 */
void dataCallbackThread(void *drvPvt)
{
  adsAsynPortDriver *pPvt = (adsAsynPortDriver *)drvPvt;
  pPvt->dataCallbackThread();
}

/** Start trigger IO Intr Callbacks thread
 * \param[in] drvPvt adsAsynPortDriver object
 * \return void
 */
void triggerEpicsIoIntrCallbacksThread(void *drvPvt)
{
  adsAsynPortDriver *pPvt = (adsAsynPortDriver *)drvPvt;
  pPvt->triggerEpicsIoIntrCallbacksThread();
}

/** Constructor for the adsAsynPortDriver class.
 * \param[in] portName Asyn port name.
 * \param[in] ipAddr Ip address of PLC.
 * \param[in] amsaddr Ams Address of PLC.
 * \param[in] amsport Default amsport in PLC (851 for first PLC).
 * \param[in] paramTableSize Maximum parameter/varaiable count.
 * \param[in] priority Asyn prio.
 * \param[in] autoConnect Enable auto connect.
 * \param[in] defaultSampleTimeMS Default sample of varaible (PLC ams router
 *            checks if variable changed, if changed then add to send buffer).
 * \param[in] maxDelayTimeMS Maximum delay before  variable that has changed is
 *            sent to client (linux). The variable can also be sent sooner if the
 *            ams router send buffer is filled.
 * \param[in] defaultTimeSource Default time stamp source of changed variable:\n
 *            defaultTimeSource=PLC: The PLC time stamp from when the value was
 *            changedis used and set as timestamp in the EPICS record
 *            (if record TSE field is set to -2 (enable asyn timestamp)).
 *            This is the preferred setting.\n
 *            defaultTimeSource=EPICS: The time stamp will be made when the
 *            updated data arrives in the EPCIS client.\n

 * Initializes all variables and tries to connect to PLC system.
 */
adsAsynPortDriver::adsAsynPortDriver(const char *portName,
                                     const char *ipaddr,
                                     const char *amsaddr,
                                     unsigned int amsport,
                                     int paramTableSize,
                                     unsigned int priority,
                                     int autoConnect,
                                     int defaultSampleTimeMS,
                                     int maxDelayTimeMS,
                                     int adsTimeoutMS,
                                     ADSTIMESOURCE defaultTimeSource)
    : asynPortDriver(portName,
                     1,                                                                                                                                                                                                                  /* maxAddr */
                     asynInt32Mask | asynFloat64Mask | asynInt64Mask | asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask | asynFloat64ArrayMask | asynDrvUserMask | asynOctetMask, /* Interface mask */
                     asynInt32Mask | asynFloat64Mask | asynInt64Mask | asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask | asynFloat64ArrayMask | asynDrvUserMask | asynOctetMask, /* Interrupt mask */
                     ASYN_CANBLOCK,                                                                                                                                                                                                      /* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */
                     0,                                                                                                                                                                                                                  /* Autoconnect */
                     priority,                                                                                                                                                                                                           /* Default priority */
                     0)                                                                                                                                                                                                                  /* Default stack size*/
{
  if (!pasynUserSelf)
  {
    throw std::runtime_error(string_format(
        "%s:%s: pasynUserSelf was a null pointer. Failure to initialize.\n",
        driverName, __func__));
  }

  // Extra Debugging from the beginning: pasynTrace->setTraceMask(pasynUserSelf, 0x11);
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  // The computer running this ioc is the client.
  // The computer (most likely a plc) this client is connecting to is the server/remote.
  // This terminology will be used throughout.

  if (paramTableSize < 1)
  {
    // If paramTableSize_==1 then only stream device or motor record can use the driver through the "default access" param below.
    throw std::runtime_error(string_format(
        "%s:%s: Param table size too small: %d\n",
        driverName, __func__, paramTableSize));
  }
  adsParamArray_.reserve(paramTableSize);

  if (!ipaddr)
  {
    throw std::runtime_error(string_format(
        "%s:%s: ip address passed was a null pointer.\n",
        driverName, __func__));
  }
  ipaddr_ = ipaddr;

  if (!amsaddr)
  {
    throw std::runtime_error(string_format(
        "%s:%s: ams address passed was a null pointer.\n",
        driverName, __func__));
  }
  amsaddr_ = amsaddr;
  remoteNetId_ = {0, 0, 0, 0, 0, 0};
  int nvals = sscanf(amsaddr_.c_str(), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
                     &remoteNetId_.b[0],
                     &remoteNetId_.b[1],
                     &remoteNetId_.b[2],
                     &remoteNetId_.b[3],
                     &remoteNetId_.b[4],
                     &remoteNetId_.b[5]);
  if (nvals != 6)
  {
    throw std::runtime_error(string_format(
        "%s:%s: AMS address invalid %s.\n",
        driverName, __func__, amsaddr_));
  }
  if (isInvalidPortNumber(amsport))
  {
    throw std::runtime_error(string_format(
        "%s:%s: invalid default ams port: %d.\n",
        driverName, __func__, amsport));
  }

  amsportDefault_ = amsport;
  addNewAmsPortToList(amsportDefault_);

  priority_ = priority;
  autoConnect_ = autoConnect;
  defaultSampleTime_ms_ = defaultSampleTimeMS;
  defaultMaxDelayTime_ms_ = maxDelayTimeMS;
  adsTimeout_ms_ = adsTimeoutMS;
  defaultTimeSource_ = defaultTimeSource;
  {
    std::lock_guard<std::mutex> lockGuard(adsAddDelRouteMutex_);
    routeEstablished_ = false;
  }
  amsPortList_.clear();

  // Octet interface
  octetAsciiBuffer_.bufferSize = ADS_CMD_BUFFER_SIZE;
  octetAsciiBuffer_.bytesUsed = 0;
  memset(&octetBinaryBuffer_, 0, ADS_CMD_BUFFER_SIZE);
  octetReturnVarName_ = 0;

  // Open a client side ads port.
  // This doesn't require any communication with the server side, it is just to establish a unique
  // identifier for the ads communication calls coming from this thread.
  // All this really does is find the lowest available port number not currently open for the ads router on this client.
  // It would only fail if we have run out of available port numbers, which is any number between
  // 1 and UINT16_MAX = 65535 inclusive.
  // Each thread will get its own port number, and we will just make sure to close them when we don't
  // need them anymore. Most likely this class's destructor or earlier if we want.
  // Open a default client port.
  if (isInvalidPortNumber(addAdsClientPortNumberForThreadId(0)))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open default ads client port: %d.\n", driverName, __func__));
  }

  // Create an ads client port that will automatically be closed when this function leaves scope.
  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    adsClientPort = getAdsClientPortNumberForThreadId(0);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: failed to get ads client port. Fallback to default client port.\n", driverName, __func__);
  }
  AmsAddr addr;
  addr.netId = remoteNetId_;
  addr.port = amsportDefault_;
  adsSymbolParserList_.emplace_back();

  // Add first param for other access (like motor record or stream device).
  int index;
  asynStatus status = createParam("Default access", asynParamNotDefined, &index);
  if (status != asynSuccess)
  {
    throw std::runtime_error(string_format("%s:%s: createParam for default access failed.\n", driverName, __func__));
  }

  adsParamArray_.emplace_back();
  auto &paramInfo = adsParamArray_.back();
  paramInfo.recordName = "Any record";
  paramInfo.recordType = "No type";
  paramInfo.scan = "No scan";
  paramInfo.dtyp = "No dtyp";
  paramInfo.inp = "No inp";
  paramInfo.out = "No out";
  paramInfo.drvInfo = "No drvinfo";
  paramInfo.asynType = asynParamNotDefined;
  paramInfo.paramIndex = index; // also used as hUser for ads callback
  paramInfo.plcAdrStr = "No adr str";

  // Create the thread that cyclically checks the state of the ads server
  // Use hardcoded cycle time of 0.5s for now.
  cyclicThreadCycleTime_s_ = 0.5;
  status = (asynStatus)(epicsThreadCreate("adsAsynPortDriverCyclicThread",
                                          epicsThreadPriorityMedium,
                                          epicsThreadGetStackSize(epicsThreadStackMedium),
                                          (EPICSTHREADFUNC)::cyclicThread, this) == NULL);
  if (status)
  {
    throw std::runtime_error(string_format("%s:%s: epicsThreadCreate failure for cyclicThread.\n", driverName, __func__));
  }

  bulkReadMaxTsEntrySize_ = 10;
  // Beckhoff advises to not use more than 500 sub commands per bulk read.
  // One sub command gets the value of one variable, so 500 sub commands is 500 variables.
  MaxNumberOfSubCallsPerBulkRead_ = 500;

  if (defaultSampleTime_ms_ < 1000)
  {
    printf("Default Sample Time of %d ms is too small, defaulting to 1Hz.\n",
           defaultSampleTime_ms_);
    bulkReadTimeDelay_us_ = 1000000; // 1 Hz
  }
  else
  {
    printf("Default bulk read time: %d ms\n", defaultSampleTime_ms_);
    bulkReadTimeDelay_us_ = defaultSampleTime_ms_ * 1000;
  }

  {
    std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);
    setOkToProcessBulkReads(false);
    bulkReadTimeElapsed_us_ = 0;
  }

  //* Create the thread that does the bulk reads */
  status = (asynStatus)(epicsThreadCreate("adsAsynPortDriverBulkReadThread",
                                          epicsThreadPriorityMedium,
                                          epicsThreadGetStackSize(epicsThreadStackMedium),
                                          (EPICSTHREADFUNC)::bulkReadThread, this) == NULL);
  if (status)
  {
    throw std::runtime_error(string_format("%s:%s: epicsThreadCreate failure for bulkReadThread.\n", driverName, __func__));
  }

  //* Create the thread that does the ADS subscription callbacks */
  status = (asynStatus)(epicsThreadCreate("adsAsynPortDriverDataCallbackThread",
                                          epicsThreadPriorityMedium,
                                          epicsThreadGetStackSize(epicsThreadStackMedium),
                                          (EPICSTHREADFUNC)::dataCallbackThread, this) == NULL);
  if (status)
  {
    throw std::runtime_error(string_format("%s:%s: epicsThreadCreate failure for dataCallbackThread.\n", driverName, __func__));
  }

  //* Create the thread that does the ADS subscription callbacks */
  status = (asynStatus)(epicsThreadCreate("adsAsynPortDriverTriggerEpicsIoIntrCallbacksThread",
                                          epicsThreadPriorityHigh,
                                          epicsThreadGetStackSize(epicsThreadStackMedium),
                                          (EPICSTHREADFUNC)::triggerEpicsIoIntrCallbacksThread, this) == NULL);
  if (status)
  {
    throw std::runtime_error(string_format("%s:%s: epicsThreadCreate failure for dataCallbackThread.\n", driverName, __func__));
  }

  // try to connect, and hang until we succeed.
  while (true)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
              "%s:%s: trying to connect to the ads server at ip = %s, amsnetid = %s...\n",
              driverName, __func__, ipaddr_.c_str(), amsaddr_.c_str());
    epicsThreadSleep(1.0);
    if (connect(pasynUserSelf) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: connect failed for the ads server at ip = %s, amsnetid = %s. Retrying in 1s...\n",
                driverName, __func__, ipaddr_.c_str(), amsaddr_.c_str());
      continue;
    }
    long error = 0;
    uint16_t adsState = 0;
    if (adsReadState(adsClientPort, amsport, adsState, true, error) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: connected to but failed to read the state of the ads server at ip = %s, amsnetid = %s. Retrying in 1s...\n",
                driverName, __func__, ipaddr_.c_str(), amsaddr_.c_str());
    }
    else if (adsState == ADSSTATE_RUN)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_INFO,
                "%s:%s: connection established for the ads server at ip = %s, amsnetid = %s.\n",
                driverName, __func__, ipaddr_.c_str(), amsaddr_.c_str());
      if (!adsSymbolParserList_[0].load(adsClientPort, addr, adsSymbolMap_))
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_INFO,
                  "%s:%s: loaded all ads symbols for port %u.\n",
                  driverName, __func__, amsportDefault_);
      }
      return;
    }
  }
}

/** Destructor for the adsAsynPortDriver class.
 * Cleanup and deallocation of variables.
 */
adsAsynPortDriver::~adsAsynPortDriver()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  auto adsClientPort = getAdsClientPortNumberForThreadId(epicsThreadGetIdSelf());

  for (auto &adsParamInfo : adsParamArray_)
  {
    adsDelDataCallback(adsClientPort, adsParamInfo, true);       // Block error messages
    adsReleaseSymbolicHandle(adsClientPort, adsParamInfo, true); // Block error messages
  }

  // For each entry still in the client port map, close the port if it is still open.
  for (auto entry : threadIdToAmsClientPortMap_)
    AdsPortCloseEx(entry.second.port);
}

/** Cyclic thread for supervision of connection.
 * \return void
 * Check ads state of all connected ams ports and reconnects if needed.
 * At reconnect all symbolic handles and callbacks will be reregistered.
 */
void adsAsynPortDriver::cyclicThread()
{
  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  while (true)
  {
    if (!allowCallbackEpicsState || !routeEstablished_)
    {
      printf("%s:%s: cyclic thread waiting to start...\n", driverName, __func__);
      epicsThreadSleep(cyclicThreadCycleTime_s_);
      continue; // Epics has not yet started, so don't start the cyclic thread yet.
    }
    {
      std::lock_guard<std::mutex> lockGuard(adsAddDelRouteMutex_);
      if (!routeEstablished_)
      {
        epicsThreadSleep(cyclicThreadCycleTime_s_);
        continue; // Route is not added yet, so don't start the cyclic thread.
      }
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: cyclic thread started with sample time = %lfs.\n", driverName, __func__, cyclicThreadCycleTime_s_);

    epicsThreadSleep(cyclicThreadCycleTime_s_);

    uint16_t adsState = 0;
    // Check state of all used ams ports
    bool allStale = true;
    for (auto &adsServerPort : amsPortList_)
    {
      long error = 0;
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: cyclic thread reading state of port %u.\n",
                driverName, __func__, adsServerPort.amsPort);
      asynStatus stat = adsReadState(adsClientPort, adsServerPort.amsPort, adsState, true, error);
      adsServerPort.connected = (stat == asynSuccess) && (adsState == ADSSTATE_RUN);
      if (adsServerPort.connected)
      {
        adsServerPort.stale = false;
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                  "%s:%s: cyclic thread read state of %s for port %u.\n",
                  driverName, __func__, adsStateToString(adsServerPort.adsState), adsServerPort.amsPort);
        adsServerPort.retryCount = 0;
      }
      else
      {
        adsServerPort.retryCount++;
      }
      if (adsServerPort.connectedOld != adsServerPort.connected)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_INFO, "%s:%s: Device \"%s\" %s (Ams-port %u, Ams router version %u.%u.%u).\n",
                  driverName, __func__, adsServerPort.devName, adsServerPort.connected ? "connected" : "disconnected",
                  adsServerPort.amsPort, adsServerPort.version.version, adsServerPort.version.revision, adsServerPort.version.build);
      }
      if (adsServerPort.adsStateOld != adsServerPort.adsState)
      {
        if (adsServerPort.paramInfo)
        {
          if (adsServerPort.paramInfo->dataSource == ADS_DATASOURCE_AMS_STATE)
          {
            void *pData = (void *)&adsServerPort.adsState;
            adsUpdateParameterLock(*adsServerPort.paramInfo, pData, 2);
          }
        }
        asynPrint(pasynUserSelf, ASYN_TRACE_INFO, "%s:%s: Ams-port, %u, state change: \"%s\" -> \"%s\".\n",
                  driverName, __func__, adsServerPort.amsPort,
                  adsStateToString(adsServerPort.adsStateOld), adsStateToString(adsServerPort.adsState));
      }
      adsServerPort.adsStateOld = adsServerPort.adsState;
      if (stat == asynSuccess)
      {
        adsServerPort.adsState = (ADSSTATE)adsState;
      }
      else
      {
        adsServerPort.adsState = ADSSTATE_INVALID;
      }

      adsServerPort.connectedOld = adsServerPort.connected;
      adsServerPort.paramsOK = adsServerPort.connected;

      if (adsServerPort.connected && adsServerPort.refreshNeeded)
      {
        refreshParamsLock(adsClientPort, adsServerPort.amsPort);
      }
      if (adsServerPort.connectedOld && !adsServerPort.connected)
      {
        invalidateParamsLock(adsServerPort.amsPort);
        adsServerPort.refreshNeeded = true;
        setAlarmPortLock(adsServerPort.amsPort, COMM_ALARM, INVALID_ALARM);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                  "%s:%s: connection failed for port %u.\n",
                  driverName, __func__, adsServerPort.amsPort);
      }
      if (!adsServerPort.connectedOld && adsServerPort.connected)
      {
        adsReadVersion(adsClientPort, adsServerPort);
      }
      if (adsServerPort.retryCount > 10)
      {
        if (!adsServerPort.stale)
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: port %u marked stale. Perhaps it does not have an ads state to read.\n",
                    driverName, __func__, adsServerPort.amsPort);
        adsServerPort.stale = true;
      }
      allStale = allStale && adsServerPort.stale;
      if (allStale)
      {
        throw std::runtime_error(string_format(
            "%s:%s: all ams server ports that this client wants to connect to are now marked stale. Giving up. Maybe try restarting the client...\n",
            driverName, __func__));
      }
    }
  }
}

/* TBD - Poll at different rates depending on pollClass! */
void adsAsynPortDriver::bulkReadThread()
{
  struct timeval start, now;
  uint32_t numBytesReadBackFromServer;
  long status;
  asynUser *asynTraceUser = getTraceAsynUser();

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  while (true)
  {
    while (!okToProcessBulkReads() || !routeEstablished_)
    {
      printf("%s:%s: bulk thread waiting to start...\n", driverName, __func__);
      epicsThreadSleep(0.5);
      continue;
    }
    asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "%s:%s: Starting bulk read.\n", driverName, __func__);
    gettimeofday(&start, NULL);
    {
      std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);
      for (auto &bulkReadInfo : bulkReadInfo_)
      {
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "%s:%s: bulk read count = %ld.\n", driverName, __func__, bulkReadInfo.numberOfVariables());
        if (bulkReadInfo.numberOfVariables() <= 0)
          continue;

        numBytesReadBackFromServer = 0;
        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = bulkReadInfo.amsPort;
        bulkReadInfo.data.resize(bulkReadInfo.readSize);
        auto reqInfoSize = sizeof(BulkReadRequestInfo) * bulkReadInfo.numberOfVariables();
        auto numSubCommandsAsIndexOffset = bulkReadInfo.numberOfVariables();
        auto indexGroupToRequestBulkRead = ADSIGRP_SUMUP_READ;
        status = AdsSyncReadWriteReqEx2(adsClientPort, &amsServer,
                                        indexGroupToRequestBulkRead, numSubCommandsAsIndexOffset,
                                        bulkReadInfo.readSize, bulkReadInfo.data.data(),
                                        reqInfoSize, bulkReadInfo.reqInfo.data(),
                                        &numBytesReadBackFromServer);

        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "%s:%s: AdsSyncReadWriteReqEx2() with params:\n", driverName, __func__);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "port: %lu\n", adsClientPort);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "pAddr: amsNetId = %s, amsServerPort = %u\n", amsaddr_.c_str(), amsServer.port);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "indexGroup: %u\n", indexGroupToRequestBulkRead);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "indexOffset: %lu\n", numSubCommandsAsIndexOffset);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "readLength: %u\n", bulkReadInfo.readSize);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "readDataLengthBytes: %u\n", bulkReadInfo.readSize);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "writeLength: %lu\n", reqInfoSize);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "writeDataLengthBytes: %lu\n", reqInfoSize);
        asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "bytesRead: %u\n", numBytesReadBackFromServer);
        if (status)
        {
          asynPrint(asynTraceUser, ASYN_TRACE_ERROR, "Bulk read failed: status %ld, client port: %ld, netid: %s, server port: %d\n",
                    status, adsClientPort, amsaddr_.c_str(), amsServer.port);
          int index = 0;
          size_t totalBytes = 0;
          for (auto &reqInfo : bulkReadInfo.reqInfo)
          {
            asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "reqInfo[#%u].iGroup = %u\n", index, reqInfo.iGroup);
            asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "reqInfo[#%u].iOffset = %u\n", index, reqInfo.iOffset);
            asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "reqInfo[#%u].iSize = %u\n", index, reqInfo.iSize);
            totalBytes += reqInfo.iSize;
            index++;
          }
          asynPrint(asynTraceUser, ASYN_TRACE_FLOW, "totalBytes = %lu\n", totalBytes);
          continue;
        }

        // The error code for each sub command is returned as the first sizeof(uint32_t) * numberOfVariables bytes.
        // The bulkReadData.data() has type uint8_t, so each index of the vector represents one byte.
        // By casting to a uint32_t, we can conveniently step along the error codes for each sub command.
        // Remember, there is one sub command per variable.
        uint32_t *errorCodePerVariable = (uint32_t *)bulkReadInfo.data.data();

        // The actual data returned from doing the bulk read is stored starting just after the error codes
        // for each sub command. So bulkReadInfo.numberOfVariables() * sizeof(uint32_t) gives us the number
        // of error code bytes, and so the dataPerVariable pointer points to the first byte that represents
        // the returned data.
        uint8_t *dataPerVariable = bulkReadInfo.data.data() + bulkReadInfo.numberOfVariables() * sizeof(uint32_t);

        uint64_t nTimeStamp = 0;
        // The first two bulk parameters might represent the first 32 bits and last 32 bits respectively of a 64 bit time stamp.
        if (!errorCodePerVariable[0] && !errorCodePerVariable[1] && bulkReadInfo.reqInfo[0].iGroup == ADSIGRP_SYM_VALBYHND)
        {
          // If there was no error in retrieving the time stamp, then piece the two 32 bit chunks together
          // to get the timestamp value.
          nTimeStamp = ((uint32_t *)dataPerVariable)[0];
          nTimeStamp = (nTimeStamp << 32) | ((uint32_t *)dataPerVariable)[1];
        }
        else
        {
          // Otherwise, just use the ioc timestamp.
          // now has the time since 1970-01-01 00:00:00 UTC, but
          // we want 100ns increments since 1601-01-01. So we convert.
          nTimeStamp = now.tv_sec + SEC_TO_UNIX_EPOCH;
          nTimeStamp = (nTimeStamp * 1000000 + now.tv_usec) * 10;
        }

        if (!errorCodePerVariable[0])
          dataPerVariable += sizeof(uint32_t);

        if (!errorCodePerVariable[1])
          dataPerVariable += sizeof(uint32_t);

        errorCodePerVariable += 2;

        for (auto paramId : bulkReadInfo.asynParamIds)
        {
          auto paramInfo = getAdsParamInfo(paramId);
          if (!paramInfo)
          {
            asynPrint(asynTraceUser, ASYN_TRACE_ERROR,
                      "%s:%s: getAdsParamInfo() for hUser %u failed\n",
                      driverName, __func__, paramId);
            errorCodePerVariable++;
            setAlarmParamLock(*paramInfo, READ_ALARM, INVALID_ALARM);
            continue;
          }
          if (*errorCodePerVariable)
          {
            asynPrint(asynTraceUser, ASYN_TRACE_ERROR,
                      "%s:%s: bulk read for %s (%s) failed\n",
                      driverName, __func__, paramInfo->drvInfo.c_str(), paramInfo->recordName.c_str());
            errorCodePerVariable++;
            setAlarmParamLock(*paramInfo, READ_ALARM, INVALID_ALARM);
            continue;
          }
          paramInfo->plcTimeStampRaw = nTimeStamp;
          paramInfo->lastCallbackSize = paramInfo->plcSize;
          paramInfo->dataBulkReadThisRead.resize(paramInfo->lastCallbackSize);
          memcpy(paramInfo->dataBulkReadThisRead.data(),
                 dataPerVariable,
                 paramInfo->dataBulkReadThisRead.size());

          if (paramInfo->dataBulkReadThisRead.size() != paramInfo->dataBulkReadLastRead.size() ||
              memcmp(dataPerVariable,
                     paramInfo->dataBulkReadLastRead.data(),
                     paramInfo->dataBulkReadThisRead.size()) != 0)
          {
            adsUpdateParameterLock(*paramInfo, dataPerVariable, true);
          }
          dataPerVariable += paramInfo->lastCallbackSize;
          errorCodePerVariable++;
          paramInfo->dataBulkReadLastRead = paramInfo->dataBulkReadThisRead;
        }
      }
      // callbackQueueShow(0);
      gettimeofday(&now, NULL);
      bulkReadTimeElapsed_us_ = (now.tv_sec - start.tv_sec) * 1000000 +
                                (now.tv_usec - start.tv_usec);

      printf("%s:%s: Bulk read complete. Elapsed time: %g\n", driverName, __func__, bulkReadTimeElapsed_us_ / 1000000.0);
      // Always sleep at least 10ms to prevent this looping from generating too many I/O interrupts.
      usleep(std::max(bulkReadTimeDelay_us_ - bulkReadTimeElapsed_us_, 10000));
    }
  }
}

/* Keeps possible slow data callbacks off of the ADS Recv queue*/
void adsAsynPortDriver::dataCallbackThread()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);
  while (true)
  {
    usleep(10000);
    if (!allowCallbackEpicsState)
      continue;
    {
      std::lock_guard<std::mutex> lockGuard(dataCbQueueMutex_);
      if (datacbqueue.empty())
        continue;
      auto &info = datacbqueue.front();
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s: Callback queue has %ld elements\n", driverName, __func__, datacbqueue.size());
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s: Run callback for parameter %s (%d).\n", driverName, __func__, info.paramInfo.drvInfo.c_str(), info.paramInfo.paramIndex);
      info.paramInfo.plcTimeStampRaw = info.notification.nTimeStamp;
      info.paramInfo.lastCallbackSize = info.notification.cbSampleSize;
      adsUpdateParameterLock(info.paramInfo, info.data.data());
      datacbqueue.pop();
    }
  }
}

// All IO Intr callbacks must be generated from this single thread.
void adsAsynPortDriver::triggerEpicsIoIntrCallbacksThread()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);
  while (true)
  {
    if (!allowCallbackEpicsState)
    {
      epicsThreadSleep(0.5);
      continue;
    }
    // Batch callback calls into 100 ms intervals
    fireAllCallbacksLock();
    usleep(100000);
  }
}

/** Report of configured parameters.
 * \param[in] fp Output file.
 * \param[in] details Details of printout. A higher number results in more
 *            details.
 * \return void
 * Check ads state of all connected ams ports and reconnects if needed.
 */
void adsAsynPortDriver::report(FILE *fp, int details)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  if (!fp)
  {
    printf("%s:%s: ERROR: File NULL.\n", driverName, __func__);
    return;
  }

  if (details >= 1)
  {
    fprintf(fp, "General information:\n");
    fprintf(fp, "  Port:                        %s\n", portName);
    fprintf(fp, "  Ip-address:                  %s\n", ipaddr_.c_str());
    fprintf(fp, "  Ams-address:                 %s\n", amsaddr_.c_str());
    fprintf(fp, "  Default Ams-port :           %d\n", amsportDefault_);
    fprintf(fp, "  Auto-connect:                %s\n", autoConnect_ ? "true" : "false");
    fprintf(fp, "  Priority:                    %d\n", priority_);
    fprintf(fp, "  Param. table size:           %ld\n", adsParamArray_.size());
    fprintf(fp, "  Param. count:                %ld\n", adsParamArray_.size());
    fprintf(fp, "  ADS command timeout [ms]:    %d\n", adsTimeout_ms_);
    fprintf(fp, "  Default sample time [ms]     %d\n", defaultSampleTime_ms_);
    fprintf(fp, "  Default max delay time [ms]: %d\n", defaultMaxDelayTime_ms_);
    fprintf(fp, "  Default time source:         %s\n", (defaultTimeSource_ == ADS_TIME_BASE_PLC) ? ADS_OPTION_TIMEBASE_PLC : ADS_OPTION_TIMEBASE_EPICS);
    fprintf(fp, "  NOTE: Several records can be linked to the same parameter.\n");
    fprintf(fp, "\n");
  }
  if (details >= 2)
  {
    // print all parameters
    fprintf(fp, "Parameter details:\n");
    for (size_t i = 0; i < adsParamArray_.size(); i++)
    {
      auto &paramInfo = adsParamArray_[i];
      fprintf(fp, "  Parameter %lu:\n", i);
      if (i == 0)
      {
        fprintf(fp, "    Parameter 0 (pasynUser->reason==0) is reserved for Asyn octet interface (Motor Record and Stream Device access).\n");
        fprintf(fp, "\n");
        continue;
      }
      fprintf(fp, "    Param name:                %s\n", paramInfo.drvInfo.c_str());
      fprintf(fp, "    Param index:               %d\n", paramInfo.paramIndex);
      fprintf(fp, "    Param type:                %s (%d)\n", asynTypeToString((long)paramInfo.asynType), paramInfo.asynType);
      fprintf(fp, "    Param sample time [ms]:    %lf\n", paramInfo.sampleTimeMS);
      fprintf(fp, "    Param max delay time [ms]: %lf\n", paramInfo.maxDelayTimeMS);
      fprintf(fp, "    Param isIOIntr:            %s\n", paramInfo.isIOIntr ? "true" : "false");
      fprintf(fp, "    Param asyn addr:           %d\n", paramInfo.asynAddr);
      fprintf(fp, "    Param time source:         %s\n", (paramInfo.timeBase == ADS_TIME_BASE_PLC) ? ADS_OPTION_TIMEBASE_PLC : ADS_OPTION_TIMEBASE_EPICS);
      fprintf(fp, "    Param plc time:            %us:%uns\n", paramInfo.plcTimeStamp.secPastEpoch, paramInfo.plcTimeStamp.nsec);
      fprintf(fp, "    Param epics time:          %us:%uns\n", paramInfo.epicsTimestamp.secPastEpoch, paramInfo.epicsTimestamp.nsec);
      fprintf(fp, "    Param array buffer alloc:  %s\n", paramInfo.arrayDataBuffer.size() ? "true" : "false");
      fprintf(fp, "    Param array buffer size:   %lu\n", paramInfo.arrayDataBuffer.size());
      fprintf(fp, "    Param alarm:               %d\n", paramInfo.alarmStatus);
      fprintf(fp, "    Param severity:            %d\n", paramInfo.alarmSeverity);
      fprintf(fp, "    Param data source:         %s\n", paramInfo.dataSource == ADS_DATASOURCE_PLC ? "PLC" : "DRIVER");
      fprintf(fp, "    Plc ams port:              %d\n", paramInfo.amsPort);
      fprintf(fp, "    Plc adr str:               %s\n", paramInfo.plcAdrStr.c_str());
      fprintf(fp, "    Plc adr str is ADR cmd:    %s\n", paramInfo.isAdrCommand ? "true" : "false");
      fprintf(fp, "    Plc abs adr valid:         %s\n", paramInfo.plcAbsAdrValid ? "true" : "false");
      fprintf(fp, "    Plc abs adr group:         16#%x\n", paramInfo.plcAbsAdrGroup);
      fprintf(fp, "    Plc abs adr offset:        16#%x\n", paramInfo.plcAbsAdrOffset);
      fprintf(fp, "    Plc data type:             %s\n", adsTypeToString(paramInfo.plcDataType));
      fprintf(fp, "    Plc data type size:        %zu\n", adsTypeSize(paramInfo.plcDataType));
      fprintf(fp, "    Plc data size:             %u\n", paramInfo.plcSize);
      fprintf(fp, "    Plc data is array:         %s\n", paramInfo.plcDataIsArray ? "true" : "false");
      fprintf(fp, "    Plc data type warning:     %s\n", paramInfo.plcDataTypeWarn ? "true" : "false");
      fprintf(fp, "    Ads hCallbackNotify:       %u\n", paramInfo.hCallbackNotify);
      fprintf(fp, "    Ads CallbackNotify valid:  %s\n", paramInfo.bCallbackNotifyValid ? "true" : "false");
      fprintf(fp, "    Ads hSymbHndle:            %u\n", paramInfo.hSymbolicHandle);
      fprintf(fp, "    Ads hSymbHndleValid:       %s\n", paramInfo.bSymbolicHandleValid ? "true" : "false");
      fprintf(fp, "    Record name:               %s\n", paramInfo.recordName.c_str());
      fprintf(fp, "    Record type:               %s\n", paramInfo.recordType.c_str());
      fprintf(fp, "    Record dtyp:               %s\n", paramInfo.dtyp.c_str());
      fprintf(fp, "\n");
    }
  }
}

/** Disconencts the PLC with asyn lock.
 * \param[in] pasynUser Asyn user.
 * \return asynSuccess or asynError.
 * Thread safe.
 */
asynStatus adsAsynPortDriver::disconnectLock(asynUser *pasynUser)
{
  lock();
  asynStatus stat = disconnect(pasynUser);
  unlock();
  return stat;
}

/** Disconencts the PLC.
 * \param[in] pasynUser Asyn user.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::disconnect(asynUser *pasynUser)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  return asynPortDriver::disconnect(pasynUser);
}

/** Refreshes the parameters that need refresh after a reconnect or a
 * connection failure to a ams port.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::refreshParams(long adsClientPort)
{
  return refreshParams(adsClientPort, 0);
}

/** Refreshes all parameters for a specific amsport (with asyn lock()).
 * \param[in] amsPort ams port.
 * \return asynSuccess or asynError.
 * Thread safe.
 */

asynStatus adsAsynPortDriver::refreshParamsLock(long adsClientPort, uint16_t amsPort)
{
  lock();
  asynStatus stat = refreshParams(adsClientPort, amsPort);
  unlock();
  return stat;
}

/** Refreshes all parameters for a specific amsport.
 * \param[in] amsPort ams port.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::refreshParams(long adsClientPort, uint16_t amsPort)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);
  if (adsParamArray_.size() > 1)
  {
    // Renew data notification callbacks
    for (size_t i = 1; i < adsParamArray_.size(); i++)
    {
      // Skip first param since used for motorrecord or stream device
      auto &paramInfo = adsParamArray_[i];
      if ((amsPort == 0 || paramInfo.amsPort == amsPort) && paramInfo.refreshNeeded)
      {
        updateParamInfoWithPLCInfo(adsClientPort, paramInfo);
      }
    }
  }
  // Renew symbols changed notification callbacks
  for (auto &port : amsPortList_)
  {
    if (port.amsPort == amsPort && port.refreshNeeded)
    {
      if (port.bCallbackNotifyValid)
      {
        adsDelSymbolsChangedCallback(adsClientPort, port);
      }
      adsAddSymbolsChangedCallback(adsClientPort, port);
    }
  }
  setOkToProcessBulkReads(allowCallbackEpicsState);
  return asynSuccess;
}

/** Invalidates all parameters for a specific amsport (with asyn lock()).
 * \param[in] amsPort ams port.
 * \return asynSuccess or asynError.
 * Thread safe.
 */
asynStatus adsAsynPortDriver::invalidateParamsLock(uint16_t amsPort)
{
  lock();
  asynStatus stat = invalidateParams(amsPort);
  unlock();
  return stat;
}

/** Invalidates all parameters for a specific amsport.
 * \param[in] amsPort ams port.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::invalidateParams(uint16_t amsPort)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);

  setOkToProcessBulkReads(false);
  for (size_t i = 1; i < adsParamArray_.size(); i++)
  {
    // Skip first param since used for motorrecord or stream device
    auto &paramInfo = adsParamArray_[i];
    if (amsPort == 0 || paramInfo.amsPort == amsPort)
    {
      paramInfo.refreshNeeded = true;
    }
  }
  for (auto &bulkTs : bulkTs_)
  {
    if (amsPort == 0 || bulkTs.amsPort == amsPort)
      bulkTs.refreshNeeded = true;
  }
  for (auto &bulkReadInfo : bulkReadInfo_)
  {
    if (amsPort == 0 || bulkReadInfo.amsPort == amsPort)
    {
      // 4 bytes for error code of first 32 bits of time stamp
      // 4 bytes for error code of second 32 bits of time stamp
      // 4 bytes for first 32 bits of time stamp
      // 4 bytes for second 32 bits of time stamp
      bulkReadInfo.readSize = 4 * sizeof(uint32_t);
    }
  }
  return asynSuccess;
}

/** Connects to a PLC (with asyn lock()).
 * \param[in] pasynUser Asyn user
 * \return asynSuccess or asynError.
 * Thread safe.
 */
asynStatus adsAsynPortDriver::connectLock(asynUser *pasynUser)
{
  lock();
  asynStatus stat = connect(pasynUser);
  unlock();
  return stat;
}

/** Connects to a PLC.
 * \param[in] pasynUser Asyn user
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::connect(asynUser *pasynUser)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: thread: %s\n", driverName, __func__, epicsThreadGetNameSelf());

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  auto result = adsConnect(adsClientPort);
  if (result != asynSuccess)
    return result;

  result = asynPortDriver::connect(pasynUser);
  if (result != asynSuccess)
    return result;

  return asynSuccess;
}

/** Validates drvInfo string
 * \param[in] drvInfo String containing information about the parameter.
 * \return asynSuccess or asynError.
 * The drvInfo string is what is after the asyn() in the "INP" or "OUT"
 * field of an record.
 */
asynStatus adsAsynPortDriver::validateDrvInfo(const char *drvInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: drvInfo: %s\n", driverName, __func__, drvInfo);

  if (strlen(drvInfo) == 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Invalid drvInfo string: Length 0 (%s).\n", drvInfo);
    return asynError;
  }

  // Check '?' mark last or '=' last
  const char *read = strrchr(drvInfo, '?');
  if (read)
  {
    if (strlen(read) == 1)
    {
      return asynSuccess;
    }
  }

  const char *write = strrchr(drvInfo, '=');
  if (write)
  {
    if (strlen(write) == 1)
    {
      return asynSuccess;
    }
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Invalid drvInfo string (%s).\n", drvInfo);
  return asynError;
}

/** Overrides asynPortDriver::drvUserCreate.
 * This function is called by the asyn-framework for each record that is linked to this asyn port.
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] drvInfo String containing information about the parameter.
 * \param[out] pptypeName
 * \param[out] psize size of pptypeName.
 * \return asynSuccess or asynError.
 * The drvInfo string is what is after the asyn() in the "INP" or "OUT"
 * field of an record.
 */
asynStatus adsAsynPortDriver::drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)
{
  static int vcnt = 0;

  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: drvInfo: %s\n", driverName, __func__, drvInfo);

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  if (validateDrvInfo(drvInfo) != asynSuccess)
  {
    return asynError;
  }

  int index = 0;
  asynStatus status = findParam(drvInfo, &index);
  if (status == asynSuccess)
  {
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Parameter index found at: %d for %s. \n", driverName, __func__, index, drvInfo);
    if (adsParamArray_[index].dataSource == ADS_DATASOURCE_AMS_STATE)
    {
      // Local variable (not in PLC) like AMS port state.
      return asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
    }
    if (adsReadParam(adsClientPort, adsParamArray_[index]) != asynSuccess)
    {
      asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: adsReadParam() failed.", driverName, __func__);
      return asynError;
    }
    return asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
  }

  if (!vcnt++)
    asynPrint(pasynUser, ASYN_TRACE_INFO, "%s:%s: linking EPICS PVs to PLC variables...\n", driverName, __func__);
  if (vcnt % 1000 == 0)
    asynPrint(pasynUser, ASYN_TRACE_INFO, "%s:%s: %d...\n", driverName, __func__, vcnt);

  // Collect data from drvInfo string and recordpasynUser->reason=index;
  adsParamArray_.emplace_back();
  auto &paramInfo = adsParamArray_.back();
  paramInfo.sampleTimeMS = defaultSampleTime_ms_;
  paramInfo.maxDelayTimeMS = defaultMaxDelayTime_ms_;
  paramInfo.refreshNeeded = true;
  paramInfo.bulkIndex = -1;
  paramInfo.bulkOffset = -1;

  if (getRecordInfoFromDrvInfo(drvInfo, paramInfo) != asynSuccess)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: getRecordInfoFromDrvInfo() failed.", driverName, __func__);
    return asynError;
  }

  if (createParam(drvInfo, paramInfo.asynType, &index) != asynSuccess)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: createParam() failed.", driverName, __func__);
    return asynError;
  }
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Parameter created: \"%s\" (index %d).\n", driverName, __func__, drvInfo, index);

  // Set default value for basic types...
  lock();
  switch (paramInfo.asynType)
  {
  case asynParamInt32:
    setIntegerParam(index, 0);
    break;
  case asynParamFloat64:
    setDoubleParam(index, 0);
    break;
  case asynParamInt64:
    setInteger64Param(index, 0);
    break;
  default:
    break;
  }
  unlock();

  paramInfo.paramIndex = index;

  int addr = 0;
  if (getAddress(pasynUser, &addr) != asynSuccess)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: getAddress() failed.", driverName, __func__);
    return asynError;
  }

  paramInfo.asynAddr = addr;

  if (parsePlcInfofromDrvInfo(drvInfo, paramInfo) != asynSuccess)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: parsePlcInfofromDrvInfo() failed.", driverName, __func__);
    return asynError;
  }
  pasynUser->timeout = (paramInfo.maxDelayTimeMS * 2) / 1000;

  if (paramInfo.dataSource != ADS_DATASOURCE_AMS_STATE)
  {
    // Do not read info from PLC if local variable (like ams-port state)
    if (updateParamInfoWithPLCInfo(adsClientPort, paramInfo) != asynSuccess)
    {
      asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: updateParamInfoWithPLCInfo() failed.", driverName, __func__);
      return asynError;
    }
  }
  return asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize); // Assigns pasynUser->reason;
}

/** Update parameter with info from PLC (variable size, type and abs addr).
 * \param[in/out] paramInfo Parameter information structure.
 * \return asynSuccess or asynError.
 * If the PLC variable is an array then a buffer is allocated in the paramInfo to
 * hold the information.
 */
asynStatus adsAsynPortDriver::updateParamInfoWithPLCInfo(long adsClientPort, adsParamInfo &paramInfo)
{
  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: : %s\n", driverName, __func__, paramInfo.drvInfo.c_str());

  // Do not read information from PLC if "variable" in driver (like ams router state)
  if (paramInfo.dataSource != ADS_DATASOURCE_PLC)
  {
    paramInfo.refreshNeeded = false;
    return asynSuccess;
  }

  // Read symbolic information if needed (to get paramInfo.plcSize)
  if (!paramInfo.isAdrCommand)
  {
    if (adsGetSymInfoByName(adsClientPort, paramInfo) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymInfoByName() failed.", driverName, __func__);
      return asynError;
    }
  }

  // Check if array
  bool isArray = false;
  switch (paramInfo.plcDataType)
  {
  case ADST_VOID:
    isArray = false;
    break;
  case ADST_STRING:
    isArray = true; // Special case
    break;
  case ADST_WSTRING:
    isArray = true; // Special case?
    break;
  case ADST_BIGTYPE:
    isArray = false;
    break;
  case ADST_MAXTYPES:
    isArray = false;
    break;
  default:
    isArray = paramInfo.plcSize > adsTypeSize(paramInfo.plcDataType);
    break;
  }
  paramInfo.plcDataIsArray = isArray;

  // Allocate memory for array
  if (isArray)
  {
    paramInfo.arrayDataBuffer.resize(paramInfo.plcSize);
    memset(paramInfo.arrayDataBuffer.data(), 0, paramInfo.plcSize);
  }

  if (!paramInfo.isAdrCommand)
  {
    adsReleaseSymbolicHandle(adsClientPort, paramInfo, true); // try to delete
    if (adsGetSymHandleByName(adsClientPort, paramInfo) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymHandleByName() failed.", driverName, __func__);
      return asynError;
    }
  }

  if (paramInfo.isIOIntr)
  {
    // If it's not a bulk read or if it's really big, just subscribe to it.
    if (!paramInfo.isBulkRead || paramInfo.plcSize > 1024 * 1024)
    {
      adsDelDataCallback(adsClientPort, paramInfo, true); // try to delete
      if (adsAddDataCallback(adsClientPort, paramInfo) != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsAddDataCallback() failed.", driverName, __func__);
        return asynError;
      }
    }
    else
    {
      // Otherwise, put it in a bulk read.
      if (adsAddToBulkRead(paramInfo) != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsAddToBulkRead() failed.", driverName, __func__);
        return asynError;
      }
    }
  }

  // Make first read
  long errorCode = 0;
  if (adsReadParam(adsClientPort, paramInfo, errorCode, 0) != asynSuccess)
  {
    // Try read again
    if (adsReadParam(adsClientPort, paramInfo, errorCode, 0) != asynSuccess)
    {
      paramInfo.refreshNeeded = true;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsReadParam() failed.", driverName, __func__);
      return asynError;
    }
  }

  paramInfo.refreshNeeded = false;
  return asynSuccess;
}

void adsAsynPortDriver::poll_info(char *name)
{
  std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);
  size_t i;
  printf("Bulk read loop: desired period = %gs, last loop time = %gs\n", bulkReadTimeDelay_us_ / 1000000.0, bulkReadTimeElapsed_us_ / 1000000.0);
  printf("Bulk read count = %lu\n", bulkReadInfo_.size());
  if (name[0] == 0)
    name = 0;
  for (i = 0; i < bulkReadInfo_.size(); i++)
  {
    printf("Bulk Read #%lu:\n", i);
    if (!name)
    {
      printf("    0: MAIN.fbSystemTime.timeLoDW (G=0x%x, O=0x%x, S=%d)\n",
             bulkReadInfo_[i].reqInfo[0].iGroup, bulkReadInfo_[i].reqInfo[0].iOffset, bulkReadInfo_[i].reqInfo[0].iSize);
      printf("    1: MAIN.fbSystemTime.timeHiDW (G=0x%x, O=0x%x, S=%d)\n",
             bulkReadInfo_[i].reqInfo[1].iGroup, bulkReadInfo_[i].reqInfo[1].iOffset, bulkReadInfo_[i].reqInfo[1].iSize);
    }
    for (size_t j = 2; j < bulkReadInfo_[i].numberOfVariables(); j++)
    {
      auto paramInfo = getAdsParamInfo(bulkReadInfo_[i].asynParamIds[j]);
      if (!paramInfo)
        continue;
      if (!name || strstr(paramInfo->plcAdrStr.c_str(), name))
        printf("  %3lu: %s (G=0x%x, O=0x%x, S=%d, TS=%d.%09d)\n", j, paramInfo->plcAdrStr.c_str(),
               bulkReadInfo_[i].reqInfo[j].iGroup, bulkReadInfo_[i].reqInfo[j].iOffset, bulkReadInfo_[i].reqInfo[j].iSize,
               paramInfo->epicsTimestamp.secPastEpoch, paramInfo->epicsTimestamp.nsec);
    }
  }
}

/* TBD - Use paramInfo.pollClass to separate into different poll rates!! */
asynStatus adsAsynPortDriver::adsAddToBulkRead(adsParamInfo &paramInfo)
{
  std::lock(adsBulkInfoUpdateMutex_, *paramInfo.mutex);
  std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_, std::adopt_lock);
  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex, std::adopt_lock);

  if (paramInfo.bulkIndex < 0)
  {
    // Not assigned yet, find one.
    size_t i;
    for (i = 0; i < bulkReadInfo_.size(); i++)
    {
      // Look for an unused entry or a non-full entry for this port.
      if (bulkReadInfo_[i].numberOfVariables() == 0 ||
          (bulkReadInfo_[i].numberOfVariables() < MaxNumberOfSubCallsPerBulkRead_ &&
           bulkReadInfo_[i].amsPort == paramInfo.amsPort))
        break;
    }
    if (i >= bulkReadInfo_.size())
    {
      bulkReadInfo_.emplace_back();
    }
    if (!bulkReadInfo_[i].numberOfVariables())
    {
      // First variable in this bulk request.
      bulkReadInfo_[i].amsPort = paramInfo.amsPort;
      bulkReadInfo_[i].reqInfo.resize(2);
      auto &first32BitsOfTimestampReqInfo = bulkReadInfo_[i].reqInfo[0];
      auto &last32BitsOfTimestampReqInfo = bulkReadInfo_[i].reqInfo[1];
      int j = adsGetBulkTimeStamp(paramInfo.amsPort);
      if (bulkTs_[j].refreshNeeded)
      {
        // The timestamp variables will occupy the first two
        // indices of the bulk read.
        first32BitsOfTimestampReqInfo.iGroup = 0x4020;
        first32BitsOfTimestampReqInfo.iOffset = 0;
        first32BitsOfTimestampReqInfo.iSize = sizeof(uint32_t);
        last32BitsOfTimestampReqInfo.iGroup = 0x4020;
        last32BitsOfTimestampReqInfo.iOffset = 0;
        last32BitsOfTimestampReqInfo.iSize = sizeof(uint32_t);
      }
      else
      {
        first32BitsOfTimestampReqInfo.iGroup = ADSIGRP_SYM_VALBYHND;
        first32BitsOfTimestampReqInfo.iOffset = bulkTs_[j].iHandleH;
        first32BitsOfTimestampReqInfo.iSize = sizeof(uint32_t);
        last32BitsOfTimestampReqInfo.iGroup = ADSIGRP_SYM_VALBYHND;
        last32BitsOfTimestampReqInfo.iOffset = bulkTs_[j].iHandleL;
        last32BitsOfTimestampReqInfo.iSize = sizeof(uint32_t);
      }
      bulkReadInfo_[i].readSize = 4 * sizeof(uint32_t);
    }
    paramInfo.bulkIndex = i;
    paramInfo.bulkOffset = bulkReadInfo_[i].numberOfVariables();
    bulkReadInfo_[i].asynParamIds.emplace_back(paramInfo.paramIndex);
    bulkReadInfo_[i].reqInfo.emplace_back();

    // Update the parameter information.
    uint32_t group, offset;
    if (paramInfo.isAdrCommand)
    {
      group = paramInfo.plcAbsAdrGroup;
      offset = paramInfo.plcAbsAdrOffset;
    }
    else
    {
      group = ADSIGRP_SYM_VALBYHND;
      offset = paramInfo.hSymbolicHandle;
    }
    bulkReadInfo_[paramInfo.bulkIndex].reqInfo[paramInfo.bulkOffset].iGroup = group;
    bulkReadInfo_[paramInfo.bulkIndex].reqInfo[paramInfo.bulkOffset].iOffset = offset;
    bulkReadInfo_[paramInfo.bulkIndex].reqInfo[paramInfo.bulkOffset].iSize = paramInfo.plcSize;

    // Increase the bulk read total number of bytes by the number of bytes plc will give us
    // for this variable plus 4 bytes for the error code.
    bulkReadInfo_[paramInfo.bulkIndex].readSize += paramInfo.plcSize + sizeof(uint32_t);
  }
  return asynSuccess;
}

int adsAsynPortDriver::adsGetBulkTimeStamp(uint16_t amsPort)
{
  std::lock_guard<std::recursive_mutex> lockGuard(adsBulkInfoUpdateMutex_);
  size_t i;
  for (i = 0; i < bulkTs_.size(); i++)
  {
    if (amsPort == bulkTs_[i].amsPort)
      break;
  }
  if (i >= bulkTs_.size())
  {
    bulkTs_.emplace_back();
    bulkTs_.back().amsPort = amsPort;
    bulkTs_.back().refreshNeeded = true;
  }
#define TSLO "MAIN.fbSystemTime.timeLoDW"
#define TSHI "MAIN.fbSystemTime.timeHiDW"
  if (bulkTs_[i].refreshNeeded)
  {
    long adsClientPort = 0;
    AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
    if (isInvalidPortNumber(adsClientPort))
    {
      throw std::runtime_error(string_format(
          "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
    }

    long statL, statH;
        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = amsPort;
    statH = AdsSyncReadWriteReqEx2(adsClientPort,
                                   &amsServer,
                                   ADSIGRP_SYM_HNDBYNAME,
                                   0,
                                   sizeof(uint32_t), &bulkTs_[i].iHandleH,
                                   strlen(TSHI), TSHI,
                                   nullptr);
    statL = AdsSyncReadWriteReqEx2(adsClientPort,
                                   &amsServer,
                                   ADSIGRP_SYM_HNDBYNAME,
                                   0,
                                   sizeof(uint32_t), &bulkTs_[i].iHandleL,
                                   strlen(TSLO), TSLO,
                                   nullptr);
    if (!statH && !statL)
      bulkTs_[i].refreshNeeded = false;
  }
  return i;
}

/** Get asyn type from record.
 * \param[in] drvInfo String containing information about the parameter.
 * \param[in/out] paramInfo Parameter information structure.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::getRecordInfoFromDrvInfo(const char *drvInfo, adsParamInfo &paramInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: drvInfo: %s\n", driverName, __func__, drvInfo);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  bool isInput = false;
  bool isOutput = false;
  paramInfo.amsPort = amsportDefault_;
  DBENTRY *pdbentry;
  pdbentry = dbAllocEntry(pdbbase);
  long status = dbFirstRecordType(pdbentry);
  bool recordFound = false;
  if (status)
  {
    dbFreeEntry(pdbentry);
    return asynError;
  }
  while (!status)
  {
    paramInfo.recordType = dbGetRecordTypeName(pdbentry);
    status = dbFirstRecord(pdbentry);
    while (!status)
    {
      paramInfo.recordName = dbGetRecordName(pdbentry);
      if (!dbIsAlias(pdbentry))
      {
        status = dbFindField(pdbentry, "INP");
        if (!status)
        {
          paramInfo.inp = dbGetString(pdbentry);
          isInput = true;
          char port[ADS_MAX_FIELD_CHAR_LENGTH];
          int adr;
          int timeout;
          char currdrvInfo[ADS_MAX_FIELD_CHAR_LENGTH];
          int nvals = sscanf(paramInfo.inp.c_str(), "@asyn(%[^,],%d,%d)%s", port, &adr, &timeout, currdrvInfo);
          if (nvals == 4)
          {
            // Ensure correct port and drvinfo
            if (strcmp(port, portName) == 0 && strcmp(drvInfo, currdrvInfo) == 0)
            {
              recordFound = true; // Correct port and drvinfo!\n");
            }
          }
        }
        else
        {
          isInput = false;
        }
        status = dbFindField(pdbentry, "OUT");
        if (!status)
        {
          paramInfo.out = dbGetString(pdbentry);
          isOutput = true;
          char port[ADS_MAX_FIELD_CHAR_LENGTH];
          int adr;
          int timeout;
          char currdrvInfo[ADS_MAX_FIELD_CHAR_LENGTH];
          int nvals = sscanf(paramInfo.out.c_str(), "@asyn(%[^,],%d,%d)%s", port, &adr, &timeout, currdrvInfo);
          if (nvals == 4)
          {
            // Ensure correct port and drvinfo
            if (strcmp(port, portName) == 0 && strcmp(drvInfo, currdrvInfo) == 0)
            {
              recordFound = true; // Correct port and drvinfo!\n");
            }
          }
        }
        else
        {
          isOutput = false;
        }

        if (recordFound)
        {
          // Correct record found. Collect data from fields
          // DTYP
          status = dbFindField(pdbentry, "DTYP");
          if (!status)
          {
            paramInfo.dtyp = dbGetString(pdbentry);
            paramInfo.asynType = dtypStringToAsynType(dbGetString(pdbentry));
          }
          else
          {
            paramInfo.dtyp = "";
            paramInfo.asynType = asynParamNotDefined;
          }

          // drvInput (not a field)
          paramInfo.drvInfo = drvInfo;
          dbFreeEntry(pdbentry);
          return asynSuccess; // The correct record was found and the paramInfo structure is filled
        }
        else
        {
          // Not correct record. Do cleanup.
          if (isInput)
          {
            paramInfo.inp = "";
          }
          if (isOutput)
          {
            paramInfo.out = "";
          }
          paramInfo.drvInfo = "";
          paramInfo.scan = "";
          paramInfo.dtyp = "";
          isInput = false;
          isOutput = false;
        }
      }
      status = dbNextRecord(pdbentry);
      paramInfo.recordName = "";
    }
    status = dbNextRecordType(pdbentry);
    paramInfo.recordType = "";
  }
  dbFreeEntry(pdbentry);
  return asynError;
}

/** Get variable information from drvInfo string.
 * \param[in] drvInfo String containing information about the parameter.
 * \param[in/out] paramInfo Parameter information structure.
 * \return asynSuccess or asynError.
 * Methods checks if input or output ('?' or '=') and parses options:
 * - "ADSPORT" (Ams port for variable)\n
 * - "T_DLY_MS" (maximum delay time ms)\n
 * - "TS_MS" (sample time ms)\n
 * - "TIMEBASE" ("PLC" or "EPICS")\n
 * Also supports the following commands:
 * - ".AMSPORTSTATE." (Read/write AMS-port state)\n
 * - ".ADR.*" (absolute access)\n
 */
asynStatus adsAsynPortDriver::parsePlcInfofromDrvInfo(const char *drvInfo, adsParamInfo &paramInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: drvInfo: %s\n", driverName, __func__, drvInfo);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  // Check if input or output
  paramInfo.isIOIntr = false;
  const char *temp = strrchr(drvInfo, '?');
  if (temp)
  {
    if (strlen(temp) == 1)
    {
      paramInfo.isIOIntr = true; // All inputs will be created I/O intr
    }
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: drvInfo %s is %s\n", driverName, __func__, drvInfo, paramInfo.isIOIntr ? "I/O Intr (end with ?)" : " not I/O Intr (end with =)");

  // take part after last "/" if option or complete string..
  char buffer[ADS_MAX_FIELD_CHAR_LENGTH];
  // See if option (find last '/')
  const char *drvInfoEnd = strrchr(drvInfo, '/');
  if (drvInfoEnd)
  { // found '/'
    int nvals = sscanf(drvInfoEnd, "/%s", buffer);
    if (nvals == 1)
    {
      paramInfo.plcAdrStr = buffer;
      paramInfo.plcAdrStr.pop_back(); // Strip ? or = from end
    }
    else
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Failed to parse PLC address string from drvInfo (%s)\n", driverName, __func__, drvInfo);
      return asynError;
    }
  }
  else
  {                                 // No options
    paramInfo.plcAdrStr = drvInfo;  // Symbolic or .ADR.
    paramInfo.plcAdrStr.pop_back(); // Strip ? or = from end
  }

  // Check if .ADR. command
  const char *option = ADS_ADR_COMMAND_PREFIX;
  paramInfo.plcAbsAdrValid = false;
  paramInfo.isAdrCommand = false;
  const char *isThere = strstr(drvInfo, option);
  if (isThere)
  {
    if (strlen(isThere) < (strlen(option) + strlen("16#%x,16#%x,%u,%u")))
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Failed to parse %s command from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
    paramInfo.isAdrCommand = true;

    int nvals = sscanf(isThere + strlen(option), "16#%x,16#%x,%u,%u",
                       &paramInfo.plcAbsAdrGroup,
                       &paramInfo.plcAbsAdrOffset,
                       &paramInfo.plcSize,
                       &paramInfo.plcDataType);

    if (nvals == 4)
    {
      paramInfo.plcAbsAdrValid = true;
    }
    else
    {
      paramInfo.plcAbsAdrValid = false;
      paramInfo.plcAbsAdrGroup = -1;
      paramInfo.plcAbsAdrOffset = -1;
      paramInfo.plcSize = -1;
      paramInfo.plcDataType = -1;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s command from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
  }

  // Check if ADS_OPTION_T_MAX_DLY_MS option
  option = ADS_OPTION_T_MAX_DLY_MS;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    if (strlen(isThere) < (strlen(option) + strlen("=0/")))
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }

    int nvals = sscanf(isThere + strlen(option), "=%lf/", &paramInfo.maxDelayTimeMS);

    if (nvals != 1)
    {
      paramInfo.maxDelayTimeMS = defaultMaxDelayTime_ms_;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
  }

  // Check if ADS_OPTION_T_SAMPLE_RATE_MS option
  option = ADS_OPTION_T_SAMPLE_RATE_MS;
  paramInfo.sampleTimeMS = defaultSampleTime_ms_;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    if (strlen(isThere) < (strlen(option) + strlen("=0/")))
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }

    int nvals = sscanf(isThere + strlen(option), "=%lf/", &paramInfo.sampleTimeMS);

    if (nvals != 1)
    {
      paramInfo.sampleTimeMS = defaultSampleTime_ms_;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
  }

  // Check if ADS_OPTION_POLLRATE option
  option = ADS_OPTION_POLLRATE;
  paramInfo.isBulkRead = false;
  paramInfo.pollClass = 1.0;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    if (strlen(isThere) < (strlen(option) + strlen("=0/")))
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
    paramInfo.isBulkRead = true;

    int nvals = sscanf(isThere + strlen(option), "=%lf/", &paramInfo.pollClass);

    if (nvals != 1)
    {
      paramInfo.pollClass = 1.0;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
  }

  // Check if ADS_OPTION_TIMEBASE option
  option = ADS_OPTION_TIMEBASE;
  paramInfo.timeBase = defaultTimeSource_;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    int minLen = strlen(ADS_OPTION_TIMEBASE_PLC);
    int epicsLen = strlen(ADS_OPTION_TIMEBASE_EPICS);
    if (epicsLen < minLen)
    {
      minLen = epicsLen;
    }
    if (strlen(isThere) < (strlen(option) + strlen("=/") + minLen))
    { // Allowed "PLC" or "EPICS"
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }

    int nvals = sscanf(isThere + strlen(option), "=%[^/]/", buffer);
    if (nvals != 1)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }

    if (strcmp(ADS_OPTION_TIMEBASE_PLC, buffer) == 0)
    {
      paramInfo.timeBase = ADS_TIME_BASE_PLC;
    }

    if (strcmp(ADS_OPTION_TIMEBASE_EPICS, buffer) == 0)
    {
      paramInfo.timeBase = ADS_TIME_BASE_EPICS;
    }
  }

  // Check if ADS_OPTION_ADSPORT option
  option = ADS_OPTION_ADSPORT;
  paramInfo.amsPort = amsportDefault_;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    if (strlen(isThere) < (strlen(option) + strlen("=0/")))
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). String to short.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
    int nvals;
    int val;
    nvals = sscanf(isThere + strlen(option), "=%d/", &val);
    if (nvals == 1)
    {
      paramInfo.amsPort = (uint16_t)val;
    }
    else
    {
      paramInfo.amsPort = amsportDefault_;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
  }

  // Check if ADS_AMS_STATE_COMMAND option Local variable/parameter (not in PLC)
  option = ADS_AMS_STATE_COMMAND;
  paramInfo.dataSource = ADS_DATASOURCE_PLC;
  isThere = strstr(drvInfo, option);
  if (isThere)
  {
    addNewAmsPortToList(paramInfo.amsPort); // Only add if not already there
    int amsPortInfoIndex = 0;
    if (getAmsPortObject(paramInfo.amsPort, amsPortInfoIndex) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed to parse %s option from drvInfo (%s). Wrong format.\n", driverName, __func__, option, drvInfo);
      return asynError;
    }
    paramInfo.dataSource = ADS_DATASOURCE_AMS_STATE; // This information is accessible in driver (not PLC)
    paramInfo.plcDataType = ADST_UINT16;
    paramInfo.plcSize = 2;
    paramInfo.plcDataIsArray = false;
    paramInfo.timeBase = ADS_TIME_BASE_EPICS;
    amsPortList_[amsPortInfoIndex].paramInfo = &paramInfo;
  }

  return addNewAmsPortToList(paramInfo.amsPort); // Only add if not already there
}

/** Get ams port information object from ams-port list.
 * \param[in] amsPort ams-port
 * \param[in] amsPortInfo amsPortInfo
 *
 * \return asynStatus
 */
asynStatus adsAsynPortDriver::getAmsPortObject(uint16_t amsPort, int &index)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: amsPort:%u\n", driverName, __func__, amsPort);

  for (size_t i = 0; i < amsPortList_.size(); i++)
  {
    if (amsPortList_[i].amsPort == amsPort)
    {
      index = i;
      return asynSuccess;
    }
  }
  return asynError;
}
/** Add new ams port to ams-port list.
 * \param[in] amsPort ams-port
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::addNewAmsPortToList(uint16_t amsPort)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: amsPort:%u\n", driverName, __func__, amsPort);

  // See if new amsPort, then update list
  for (auto &port : amsPortList_)
  {
    if (port.amsPort == amsPort)
    {
      return asynSuccess;
    }
  }

  try
  {
    amsPortList_.emplace_back();
    auto &amsPortInfo = amsPortList_.back();
    amsPortInfo.amsPort = amsPort;
    amsPortInfo.connected = 0;
    amsPortInfo.connectedOld = 0;
    amsPortInfo.paramsOK = 0;
    amsPortInfo.version = AdsVersion();
    memset(amsPortInfo.devName, 0, sizeof(amsPortInfo.devName));
    amsPortInfo.adsState = (ADSSTATE)(ADSSTATE_MAXSTATES + 1);
    amsPortInfo.adsStateOld = amsPortInfo.adsState;
    amsPortInfo.paramInfo = nullptr;
    amsPortInfo.hCallbackNotify = 0;
    amsPortInfo.bCallbackNotifyValid = false;
    amsPortInfo.refreshNeeded = false;
    amsPortInfo.retryCount = 0;
  }
  catch (std::exception &e)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Failed to add new amsPort to list. Exception: %s.\n", driverName, __func__, e.what());
    return asynError;
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Added new amsPort to amsPortList: %d .\n", driverName, __func__, amsPort);

  return asynSuccess;
}

/** Checks if callback is allowed for a certain parameter.
 * \param[in] paramInfo Parameter info structure.
 *
 * \return true if parameter information and ams-port connection is OK
 *  otherwise false.
 */
bool adsAsynPortDriver::isCallbackAllowed(adsParamInfo &paramInfo)
{
  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: isCallbackAllowed = %d\n", driverName, __func__, !paramInfo.refreshNeeded);

  return !paramInfo.refreshNeeded;
}

/** Checks if callback is allowed for a certain ams-port.
 * \param[in] amsPort amsPort.
 *
 * \return true if connection to ams-port is ok otherwise false.
 */
bool adsAsynPortDriver::isCallbackAllowed(uint16_t amsPort)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: amsPort = %u\n", driverName, __func__, amsPort);

  for (auto port : amsPortList_)
  {
    if (port.amsPort == amsPort)
      return port.paramsOK;
  }
  return false;
}

/** Overrides asynPortDriver:readOctet.
 * This method, together with writeOctet, implements an ASCII command parser.
 * Mainly used for motor record and stream device access. pasynUser->reason==0
 * is reserved for this interface (and also pAdsParamArray_[0]).
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Buffer for read data.
 * \param[in] maxChars Size of value buffer.
 * \param[out] nActual Actual written chars to buffer.
 * \param[out] eomReason Read completed or not (Buffer to small
 * results in more reads needed).
 *
 * \return asynSuccess or asynError.
 *
 * \note: Example of a few ASCII commands:\n
 *  1. Symbolic read: "option1/option2/symbolicname?;":\n
 *      Read a var on ams-port 851: "ADSPORT=851/Main.M1.fPosition?;"\n
 *  2. Symbolic write: "option1/option2/symbolicname=<value>;":\n
 *      Write to a var on ams-port 851: "ADSPORT=851/Main.M1.fPosition=10;"\n
 *  3: Abs address read: "option1/.ADR.16#<group>,<offset>,<size>,<type>?;"\n
 *      Read low soflimit position in TwinCAT NC for axis 1:\n
 *      "ADSPORT=501/.ADR.16#5001,D,8,5?;"\n
 *  4: Abs address write: "option1/.ADR.16#<group>,<offset>,<size>,<type>=<value>;"\n
 *      Set low soflimit position in TwinCAT NC for axis 1 to 100:\n
 *      "ADSPORT=501/.ADR.16#5001,D,8,5=100;"\n
 */
asynStatus adsAsynPortDriver::readOctet(asynUser *pasynUser, char *value, size_t maxChars, size_t *nActual, int *eomReason)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  size_t thisRead = 0;
  int reason = 0;
  asynStatus status = asynSuccess;

  *value = '\0';
  lock();
  int error = octetCMDreadIt(adsClientPort, value, maxChars);
  if (error)
  {
    status = asynError;
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s:%s: error, CMDreadIt failed (0x%x).\n",
              driverName, __func__, error);
    unlock();
    return asynError;
  }

  thisRead = strlen(value);
  *nActual = thisRead;

  /* May be not enough space ? */
  if (thisRead > maxChars - 1)
  {
    reason |= ASYN_EOM_CNT;
  }
  else
  {
    reason |= ASYN_EOM_EOS;
  }

  if (thisRead == 0 && pasynUser->timeout == 0)
  {
    status = asynTimeout;
  }

  if (eomReason)
  {
    *eomReason = reason;
  }

  *nActual = thisRead;
  asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s thisRead=%lu data=\"%s\"\n",
            portName,
            (unsigned long)thisRead, value);
  unlock();
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:%s\n", driverName, __func__, value);

  return status;
}

/** Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] outbuf Buffer for read data.
 * \param[in] outlen Size of value buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetCMDreadIt(long adsClientPort, char *outbuf, size_t outlen)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Buffer: %s, size: %d\n", driverName, __func__, outbuf, (int)outlen);

  int ret;
  if (!outbuf || !outlen)
  {
    return -1;
  }
  ret = snprintf(outbuf, outlen + 1, "%s", octetAsciiBuffer_.buffer);

  if (ret < 0)
  {
    octetClearBuffer(&octetAsciiBuffer_);
    return ret;
  }

  if (ret >= (int)outlen + 1)
  {
    ret = outlen;
  }
  octetRemoveFromBuffer(&octetAsciiBuffer_, ret);

  return 0;
}

/** Overrides asynPortDriver::writeOctet.
 * This method, together with readOctet, implements an ASCII command parser.
 * Mainly used for motor record and stream device access. pasynUser->reason==0
 * is reserved for this interface (and also pAdsParamArray_[0]).
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Buffer for read data.
 * \param[in] maxChars Size of value buffer.
 * \param[out] nActual Actual written chars to buffer.
 *
 * \return asynSuccess or asynError.
 *
 * \note: Example of a few ASCII commands:\n
 *  1. Symbolic read: "option1/option2/symbolicname?;":\n
 *      Read a var on ams-port 851: "ADSPORT=851/Main.M1.fPosition?;"\n
 *  2. Symbolic write: "option1/option2/symbolicname=<value>;":\n
 *      Write to a var on ams-port 851: "ADSPORT=851/Main.M1.fPosition=10;"\n
 *  3: Abs adress read: "option1/.ADR.16#<group>,<offset>,<size>,<type>?;"\n
 *      Read low softlimit position in TwinCAT NC for axis 1:\n
 *      "ADSPORT=501/.ADR.16#5001,D,8,5?;"\n
 *  4: Abs adress write: "option1/.ADR.16#<group>,<offset>,<size>,<type>=<value>;"\n
 *      Set low softlimit position in TwinCAT NC for axis 1 to 100:\n
 *      "ADSPORT=501/.ADR.16#5001,D,8,5=100;"\n
 */
asynStatus adsAsynPortDriver::writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: %s\n", driverName, __func__, value);

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  size_t thisWrite = 0;
  asynStatus status = asynError;

  asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s write.\n", portName);
  asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, value, maxChars,
              "%s write %lu\n",
              portName,
              (unsigned long)maxChars);
  *nActual = 0;

  if (maxChars == 0)
  {
    return asynSuccess;
  }
  // lock();
  int errorCode = octetCMDwriteIt(adsClientPort, value, maxChars);
  if (errorCode)
  {
    /*Return asyn error if communication is down (all client errors) otherwise asynSuccess
     * but error message in buffer*/
    if (errorCode >= ADSERR_CLIENT_ERROR)
    {
      return asynError;
    }
  }
  status = asynSuccess;
  thisWrite = maxChars;
  *nActual = thisWrite;

  // unlock();
  asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s wrote %lu return %s.\n",
            portName,
            (unsigned long)*nActual,
            pasynManager->strStatus(status));
  return status;
}

/** Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] inbuf Buffer for read data.
 * \param[in] inlen Size of value buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetCMDwriteIt(long adsClientPort, const char *inbuf, size_t inlen)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Write command: %s, length: %d\n", driverName, __func__, inbuf, (int)inlen);

  int had_cr = 0;
  int had_lf = 0;
  int errorCode;
  char *new_buf = (char *)inbuf;
  if (!inbuf || !inlen)
    return -1;

  new_buf = (char *)malloc(inlen + 1);
  memcpy(new_buf, inbuf, inlen);
  new_buf[inlen] = 0;

  if (inlen > 1 && new_buf[inlen - 1] == '\n')
  {
    had_lf = 1;
    new_buf[inlen - 1] = '\0';
    inlen--;
    if (inlen > 1 && new_buf[inlen - 1] == '\r')
    {
      had_cr = 1;
      new_buf[inlen - 1] = '\0';
      inlen--;
    }
  }

  errorCode = octetCmdHandleInputLine(adsClientPort, new_buf, &octetAsciiBuffer_);
  free(new_buf);

  octetCmdBuf_printf(&octetAsciiBuffer_, "%s%s", had_cr ? "\r" : "", had_lf ? "\n" : "");

  return errorCode;
}

int adsAsynPortDriver::octetCmdHandleInputLine(long adsClientPort, const char *input_line, adsOctetOutputBufferType *buffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Input line: %s\n", driverName, __func__, input_line);

  const char **my_argv = NULL;
  char **my_sepv = NULL;
  int argc = octetCreateArgvSepv(input_line,
                                 (const char ***)&my_argv,
                                 (char ***)&my_sepv);

  int errorCodeLatch = 0;
  for (int i = 1; i <= argc; i++)
  {
    int errorCode = octetMotorHandleOneArg(adsClientPort, my_argv[i], buffer); // Continue with next cmd even if error
    if (errorCode && !errorCodeLatch)
    { // latch first error code for stacked commands
      errorCodeLatch = errorCode;
    }
    octetCmdBuf_printf(buffer, "%s", my_sepv[i]);
  }

  for (int i = 0; i <= argc; i++)
  {
    free((void *)my_argv[i]);
    free((void *)my_sepv[i]);
  }
  free(my_argv);
  free(my_sepv);

  return errorCodeLatch; // First encountered error code
}

/** Parse one ascii command.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] myarg_1 Command to parse.
 * \param[out] buffer Output buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetMotorHandleOneArg(long adsClientPort, const char *myarg_1, adsOctetOutputBufferType *buffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Command: %s\n", driverName, __func__, myarg_1);

  // const char *myarg = myarg_1;
  int err_code = 0;

  uint16_t amsPort = amsportDefault_; // should actually be called amsport ( 851 for first plc as default) ...

  /* ADSPORT= */
  if (!strncmp(myarg_1, ADS_OPTION_ADSPORT, strlen(ADS_OPTION_ADSPORT)))
  {
    myarg_1 += strlen(ADS_OPTION_ADSPORT) + 1; //+1 Because equal sign
    int nvals = sscanf(myarg_1, "%" SCNu16, &amsPort);
    if (nvals != 1)
    {
      err_code = ADS_COM_ERROR_OCTET_ADSPORT_OPTION_FAIL;
      OCTET_RETURN_ERROR(buffer, err_code, "%s", "ADS_COM_ERROR_OCTET_ADSPORT_OPTION_FAIL");
    }
    myarg_1 = strchr(myarg_1, '/');
    myarg_1++;
  }

  /* .THIS.sFeatures? */
  if (0 == strcmp(myarg_1, ADS_OCTET_FEATURES_COMMAND))
  {
#ifdef DUT_AXIS_STATUS
    const char *feature_str = "ads;stv1";
#else
    const char *feature_str = "ads";
#endif
    octetCmdBuf_printf(buffer, "%s", feature_str);
    return 0;
  }

  /*.ADR.*/
  const char *adr = strstr(myarg_1, ADS_ADR_COMMAND_PREFIX);
  if (adr)
  {
    myarg_1 = adr;

    err_code = octetMotorHandleADRCmd(adsClientPort, myarg_1, amsPort, buffer);
    if (err_code == -1 || err_code == 0)
    {
      return 0;
    }
    OCTET_RETURN_ERROR(buffer, err_code, "%s\n", adsErrorToString(err_code));
  }

  char variableName[255];
  memset(&variableName, 0, sizeof(variableName));

  // symbolic write
  adr = strchr(myarg_1, '=');
  if (adr)
  {
    // Copy variable name
    strncpy(variableName, myarg_1, adr - myarg_1);
    adr++; // Jump over '='
    err_code = octetAdsWriteByName(adsClientPort, amsPort, variableName, adr, buffer);
    if (err_code)
    {
      OCTET_RETURN_ERROR(buffer, err_code, "%s", adsErrorToString(err_code));
    }
    octetCmdBuf_printf(buffer, "OK");
    return 0;
  }

  // symbolic read
  adr = strchr(myarg_1, '?');
  if (adr)
  {
    // Copy variable name
    strncpy(variableName, myarg_1, adr - myarg_1);
    variableName[adr - myarg_1] = 0;
    err_code = octetAdsReadByName(adsClientPort, amsPort, variableName, buffer);
    if (err_code)
    {
      OCTET_RETURN_ERROR(buffer, err_code, "%s", adsErrorToString(err_code));
    }
    return 0;
  }
  /*  if we come here, it is a bad command */
  octetCmdBuf_printf(buffer, "Error: Bad command");
  return 0;
}

/** Parse one ASCII .ADR. command.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] arg Command to parse.
 * \param[in] amsport Ams-port.
 * \param[out] buffer Output buffer.
 *
 * \return 0 for success or error code.
 *
 * \note:  see octetAdsWriteByGroupOffset for more information.\n
 */
int adsAsynPortDriver::octetMotorHandleADRCmd(long adsClientPort, const char *arg, uint16_t amsport, adsOctetOutputBufferType *buffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Command: %s, amsPort: %d\n", driverName, __func__, arg, (int)amsport);

  const char *myarg_1 = NULL;
  unsigned group_no = 0;
  unsigned offset_in_group = 0;
  unsigned len_in_PLC = 0;
  unsigned type_in_PLC = 0;
  int nvals;
  nvals = sscanf(arg, ".ADR.16#%x,16#%x,%u,%u=",
                 &group_no,
                 &offset_in_group,
                 &len_in_PLC,
                 &type_in_PLC);

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: nvals=%d amsport=%u group_no=0x%x offset_in_group=0x%x len_in_PLC=%u type_in_PLC=%u\n",
            driverName,
            __func__,
            nvals,
            amsport,
            group_no,
            offset_in_group,
            len_in_PLC,
            type_in_PLC);

  if (nvals != 4)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Failed to parse .ADR. command.\n", driverName, __func__);
    return __LINE__;
  }

  // WRITE
  myarg_1 = strchr(arg, '=');
  if (myarg_1)
  {
    myarg_1++; /* Jump over '=' */

    int error = octetAdsWriteByGroupOffset(adsClientPort, amsport, (uint32_t)group_no, (uint32_t)offset_in_group, (uint16_t)type_in_PLC, (uint32_t)len_in_PLC, myarg_1, buffer);
    if (error)
    {
      OCTET_RETURN_ERROR(buffer, error, "%s", adsErrorToString(error));
    }
    octetCmdBuf_printf(buffer, "OK");
    return 0;
  }

  // READ
  myarg_1 = strchr(arg, '?');
  if (myarg_1)
  {
    myarg_1++; /* Jump over '?' */
    AdsSymbolEntryExpanded info;
    info.dataType = type_in_PLC;
    info.size = len_in_PLC;
    info.iGroup = group_no;
    info.iOffs = offset_in_group;

    int error = octetAdsReadByGroupOffset(adsClientPort, amsport, info, buffer);
    if (error)
    {
      OCTET_RETURN_ERROR(buffer, error, "%s", adsErrorToString(error));
    }
    return 0;
  }
  return __LINE__;
}

/** Read a variable from PLC by symbolic addressing.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] amsport Ams-port.
 * \param[in] variableAddr Variable name ("Main.fTest")
 * \param[out] outBuffer Output buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetAdsReadByName(long adsClientPort, uint16_t amsPort, const char *variableAddr, adsOctetOutputBufferType *outBuffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Variable:%s, amsPort %u\n", driverName, __func__, variableAddr, amsPort);

  AdsSymbolEntryExpanded infoStruct;

  long errorCode = 0;
  asynStatus stat = adsGetSymInfoByName(adsClientPort, amsPort, variableAddr, infoStruct, &errorCode);
  if (stat != asynSuccess)
  {
    return errorCode;
  }

  return octetAdsReadByGroupOffset(adsClientPort, amsPort, infoStruct, outBuffer);
}

/** Write a variable to PLC by symbolic addressing.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] amsport Ams-port.
 * \param[in] variableAddr Variable name ("Main.fTest")
 * \param[in] asciiValueToWrite Value to write in string format.
 * \param[out] outBuffer Output buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetAdsWriteByName(long adsClientPort, uint16_t amsPort, const char *variableAddr, const char *asciiValueToWrite, adsOctetOutputBufferType *outBuffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Variable: %s, value: %s.\n", driverName, __func__, variableAddr, asciiValueToWrite);

  AdsSymbolEntryExpanded infoStruct;

  long errorCode = 0;
  asynStatus stat = adsGetSymInfoByName(adsClientPort, amsPort, variableAddr, infoStruct, &errorCode);
  if (stat != asynSuccess)
  {
    return errorCode;
  }

  return octetAdsWriteByGroupOffset(adsClientPort, amsPort, infoStruct.iGroup, infoStruct.iOffs, infoStruct.dataType, infoStruct.size, asciiValueToWrite, outBuffer);
}

/**Read a variable from PLC by absolute addressing.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] amsport Ams-port.
 * \param[in] info Variable information.
 * \param[out] outBuffer Output buffer.
 *
 * \return 0 for success or error code.
 */
int adsAsynPortDriver::octetAdsReadByGroupOffset(long adsClientPort, uint16_t amsPort, AdsSymbolEntryExpanded &info, adsOctetOutputBufferType *outBuffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: amsPort: %d, group: %d, offset: %d, dataType: %s (%d), dataSize: %d.\n",
            driverName, __func__, (int)amsPort, (int)info.iGroup, (int)info.iOffs,
            adsTypeToString(info.dataType), (int)info.dataType, (int)info.size);

  uint32_t bytesRead = 0;
        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = amsPort;

  int dataSize = info.size;
  if (info.size > ADS_CMD_BUFFER_SIZE)
  {
    dataSize = ADS_CMD_BUFFER_SIZE;
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s: Read buffer size smaller than size in plc.\n", driverName, __func__);
  }

  memset(&octetBinaryBuffer_, 0, ADS_CMD_BUFFER_SIZE);

  int error = AdsSyncReadReqEx2(adsClientPort, &amsServer, info.iGroup, info.iOffs, dataSize, &octetBinaryBuffer_, &bytesRead);

  if (error)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ADS read failed with: %s (0x%x).\n", driverName, __func__, adsErrorToString(error), error);
    return error;
  }

  error = octetBinary2ascii(octetReturnVarName_, &octetBinaryBuffer_, ADS_CMD_BUFFER_SIZE, info, outBuffer);
  if (error)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Binary to ASCII conversion failed with: %d\n", driverName, __func__, error);
    return error;
  }
  return 0;
}

/**Write a variable to PLC by absolute addressing.\
 * Implements part of the asyn-octet ASCII command parser.
 * (see readOctet() and writeOctet for more info).
 * \param[in] amsport Ams-port.
 * \param[in] group Group (address).
 * \param[in] offset Offset in group (address).
 * \param[in] dataType Data type to write (address).
 * \param[in] dataSize Bytes to write.
 * \param[out] asciiResponseBuffer Output buffer.
 *
 * \return 0 for success or error code.
 *
 * \note: dataType is defined in the adsLib as:
 *   Name:         dataType:  dataSize/element (bytes):\n
 *   ADST_VOID     0          0\n
 *   ADST_INT8     16         1\n
 *   ADST_UINT8    17         1\n
 *   ADST_INT16    2          2\n
 *   ADST_UINT16   18         2\n
 *   ADST_INT32    3          4\n
 *   ADST_UINT32   19         4\n
 *   ADST_INT64    20         8\n
 *   ADST_UINT64   21         8\n
 *   ADST_REAL32   4          4\n
 *   ADST_REAL64   5          8\n
 *   ADST_BIGTYPE  65         NAN\n
 *   ADST_STRING   30         1\n
 *   ADST_WSTRING  31         1\n
 *   ADST_REAL80   32         10\n
 *   ADST_BIT      33         1\n
 *   \n
 *   The data will be considered to be an array if dataSize is bigger than the\n
 *   size of the the type.
 */
int adsAsynPortDriver::octetAdsWriteByGroupOffset(long adsClientPort, uint16_t amsPort, uint32_t group, uint32_t offset, uint16_t dataType, uint32_t dataSize, const char *asciiValueToWrite, adsOctetOutputBufferType *asciiResponseBuffer)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: amsPort: %d, group: %d, offset: %d, dataType: %s (%d), dataSize: %d.\n", driverName, __func__, (int)amsPort, (int)group, (int)offset, adsTypeToString(dataType), (int)dataType, (int)dataSize);

  uint32_t bytesToWrite = 0;
        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = amsPort;

  memset(&octetBinaryBuffer_, 0, ADS_CMD_BUFFER_SIZE);

  int error = octetAscii2binary(asciiValueToWrite, dataType, &octetBinaryBuffer_, ADS_CMD_BUFFER_SIZE, &bytesToWrite);
  if (error)
  {
    octetCmdBuf_printf(asciiResponseBuffer, "Error: %x", error);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ASCII to binary conversion failed with: %d.\n", driverName, __func__, error);
    return error;
  }

  if (bytesToWrite > dataSize)
  {
    bytesToWrite = dataSize;
  }

  error = AdsSyncWriteReqEx(adsClientPort, &amsServer, group, offset, bytesToWrite, &octetBinaryBuffer_);

  if (error)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ADS write failed with: %s (0x%x).\n", driverName, __func__, adsErrorToString(error), error);
    return error;
  }

  return 0;
}

/** Overrides asynPortDriver::writeInt32.
 * Writes int32 to PLC
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Value to write.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  int paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: invalid parameter index = %d\n", driverName, __func__, paramIndex);
    pasynUser->alarmStatus = WRITE_ALARM;
    return asynError;
  }

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  // Special case. Check if write ams port state
  if (paramInfo.dataSource == ADS_DATASOURCE_AMS_STATE)
  {
    if (adsWriteState(adsClientPort, paramInfo.amsPort, (uint16_t)value) != asynSuccess)
    {
      return setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    }
    // Write OK -> reset write alarm
    if (paramInfo.alarmStatus == WRITE_ALARM)
    {
      return setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
    }
    return asynSuccess;
  }

  uint8_t buffer[8]; // largest datatype is 8bytes
  uint32_t maxBytesToWrite = 0;
  // Convert epicsInt32 to plctype if possible..
  switch (paramInfo.plcDataType)
  {
  case ADST_INT8:
    int8_t *ADST_INT8Var;
    ADST_INT8Var = ((int8_t *)buffer);
    *ADST_INT8Var = (int8_t)value;
    maxBytesToWrite = 1;
    break;
  case ADST_INT16:
    int16_t *ADST_INT16Var;
    ADST_INT16Var = ((int16_t *)buffer);
    *ADST_INT16Var = (int16_t)value;
    maxBytesToWrite = 2;
    break;
  case ADST_INT32:
    int32_t *ADST_INT32Var;
    ADST_INT32Var = ((int32_t *)buffer);
    *ADST_INT32Var = (int32_t)value;
    maxBytesToWrite = 4;
    break;
  case ADST_UINT8:
    uint8_t *ADST_UINT8Var;
    ADST_UINT8Var = ((uint8_t *)buffer);
    *ADST_UINT8Var = (uint8_t)value;
    maxBytesToWrite = 1;
    break;
  case ADST_UINT16:
    uint16_t *ADST_UINT16Var;
    ADST_UINT16Var = ((uint16_t *)buffer);
    *ADST_UINT16Var = (uint16_t)value;
    maxBytesToWrite = 2;
    break;
  case ADST_UINT32:
    uint32_t *ADST_UINT32Var;
    ADST_UINT32Var = ((uint32_t *)buffer);
    *ADST_UINT32Var = (uint32_t)value;
    maxBytesToWrite = 4;
    break;
  case ADST_REAL32:
    float *ADST_REAL32Var;
    ADST_REAL32Var = ((float *)buffer);
    *ADST_REAL32Var = (float)value;
    maxBytesToWrite = 4;
    break;
  case ADST_REAL64:
    double *ADST_REAL64Var;
    ADST_REAL64Var = ((double *)buffer);
    *ADST_REAL64Var = (double)value;
    maxBytesToWrite = 8;
    break;
  case ADST_BIT:
    buffer[0] = value > 0;
    maxBytesToWrite = 1;
    break;
  default:
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types not compatible (epicsInt32 and %s). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType));
    return asynError;
    break;
  }

  // Warning. Risk of loss of data..
  if (sizeof(value) > maxBytesToWrite || sizeof(value) > paramInfo.plcSize)
  {
    asynPrint(pasynUser, ASYN_TRACE_WARNING, "%s:%s: WARNING. EPICS datatype size larger than PLC datatype size (%ld vs %d bytes).\n", driverName, __func__, sizeof(value), paramInfo.plcSize);
    paramInfo.plcDataTypeWarn = true;
  }

  // Ensure that PLC datatype and number of bytes to write match
  if (maxBytesToWrite != paramInfo.plcSize || maxBytesToWrite == 0)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types size missmatch (%s and %d bytes). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType), maxBytesToWrite);
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  // Do the write
  if (adsWriteParam(adsClientPort, paramInfo, (const void *)buffer, maxBytesToWrite) != asynSuccess)
  {
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }
  // Only reset if write alarm
  if (paramInfo.alarmStatus == WRITE_ALARM)
  {
    setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  }

  return asynPortDriver::writeInt32(pasynUser, value);
}

asynStatus adsAsynPortDriver::writeInt64(asynUser *pasynUser, epicsInt64 value)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  int paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: invalid parameter index = %d\n", driverName, __func__, paramIndex);
    pasynUser->alarmStatus = WRITE_ALARM;
    return asynError;
  }

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  // Special case: AMS port state
  if (paramInfo.dataSource == ADS_DATASOURCE_AMS_STATE)
  {
    if (adsWriteState(adsClientPort, paramInfo.amsPort, (uint16_t)value) != asynSuccess)
    {
      return setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    }
    if (paramInfo.alarmStatus == WRITE_ALARM)
    {
      return setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
    }
    return asynSuccess;
  }

  uint8_t buffer[8]; // 8 bytes for int64_t/uint64_t/double
  uint32_t maxBytesToWrite = 0;

  switch (paramInfo.plcDataType)
  {
  case ADST_INT64:
  {
    int64_t *ADST_INT64Var = (int64_t *)buffer;
    *ADST_INT64Var = (int64_t)value;
    maxBytesToWrite = 8;
    break;
  }
  case ADST_UINT64:
  {
    uint64_t *ADST_UINT64Var = (uint64_t *)buffer;
    *ADST_UINT64Var = (uint64_t)value; // User beware: signed->unsigned cast!
    maxBytesToWrite = 8;
    break;
  }
  default:
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types not compatible (epicsInt64 and %s). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType));
    return asynError;
  }

  // Sanity: Check PLC buffer sizes
  if (sizeof(value) > maxBytesToWrite || sizeof(value) > paramInfo.plcSize)
  {
    asynPrint(pasynUser, ASYN_TRACE_WARNING, "%s:%s: WARNING. EPICS datatype size larger than PLC datatype size (%ld vs %d bytes).\n",
              driverName, __func__, sizeof(value), paramInfo.plcSize);
    paramInfo.plcDataTypeWarn = true;
  }

  // Ensure match
  if (maxBytesToWrite != paramInfo.plcSize || maxBytesToWrite == 0)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types size mismatch (%s and %d bytes). Write canceled.\n",
              driverName, __func__, adsTypeToString(paramInfo.plcDataType), maxBytesToWrite);
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  // Write the value
  if (adsWriteParam(adsClientPort, paramInfo, buffer, maxBytesToWrite) != asynSuccess)
  {
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }
  if (paramInfo.alarmStatus == WRITE_ALARM)
  {
    setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  }

  return asynPortDriver::writeInt64(pasynUser, value);
}

/** Overrides asynPortDriver::writeFloat64.
 * Writes float64 to PLC
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Value to write.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  int paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: pAdsParamArray NULL\n", driverName, __func__);
    pasynUser->alarmStatus = WRITE_ALARM;
    pasynUser->alarmSeverity = INVALID_ALARM;
    return asynError;
  }

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  // Special case. Check if write ams port state
  if (paramInfo.dataSource == ADS_DATASOURCE_AMS_STATE)
  {
    if (adsWriteState(adsClientPort, paramInfo.amsPort, (uint16_t)value) != asynSuccess)
    {
      return setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    }
    // Write OK -> reset write alarm
    if (paramInfo.alarmStatus == WRITE_ALARM)
    {
      return setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
    }
    return asynSuccess;
  }

  uint8_t buffer[8]; // largest datatype is 8bytes
  uint32_t maxBytesToWrite = 0;
  // Convert epicsFloat64 to plctype if possible..
  switch (paramInfo.plcDataType)
  {
  case ADST_INT8:
    int8_t *ADST_INT8Var;
    ADST_INT8Var = ((int8_t *)buffer);
    *ADST_INT8Var = (int8_t)value;
    maxBytesToWrite = 1;
    break;
  case ADST_INT16:
    int16_t *ADST_INT16Var;
    ADST_INT16Var = ((int16_t *)buffer);
    *ADST_INT16Var = (int16_t)value;
    maxBytesToWrite = 2;
    break;
  case ADST_INT32:
    int32_t *ADST_INT32Var;
    ADST_INT32Var = ((int32_t *)buffer);
    *ADST_INT32Var = (int32_t)value;
    maxBytesToWrite = 4;
    break;
  case ADST_INT64:
    int64_t *ADST_INT64Var;
    ADST_INT64Var = ((int64_t *)buffer);
    *ADST_INT64Var = (int64_t)value;
    maxBytesToWrite = 8;
    break;
  case ADST_UINT8:
    uint8_t *ADST_UINT8Var;
    ADST_UINT8Var = ((uint8_t *)buffer);
    *ADST_UINT8Var = (uint8_t)value;
    maxBytesToWrite = 1;
    break;
  case ADST_UINT16:
    uint16_t *ADST_UINT16Var;
    ADST_UINT16Var = ((uint16_t *)buffer);
    *ADST_UINT16Var = (uint16_t)value;
    maxBytesToWrite = 2;
    break;
  case ADST_UINT32:
    uint32_t *ADST_UINT32Var;
    ADST_UINT32Var = ((uint32_t *)buffer);
    *ADST_UINT32Var = (uint32_t)value;
    maxBytesToWrite = 4;
    break;
  case ADST_UINT64:
    uint64_t *ADST_UINT64Var;
    ADST_UINT64Var = ((uint64_t *)buffer);
    *ADST_UINT64Var = (uint64_t)value;
    maxBytesToWrite = 8;
    break;
  case ADST_REAL32:
    float *ADST_REAL32Var;
    ADST_REAL32Var = ((float *)buffer);
    *ADST_REAL32Var = (float)value;
    maxBytesToWrite = 4;
    break;
  case ADST_REAL64:
    double *ADST_REAL64Var;
    ADST_REAL64Var = ((double *)buffer);
    *ADST_REAL64Var = (double)value;
    maxBytesToWrite = 8;
    break;
  case ADST_BIT:
    buffer[0] = value > 0;
    maxBytesToWrite = 1;
    break;
  default:
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types not compatible (epicsInt32 and %s). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType));
    return asynError;
    break;
  }

  // Warning. Risk of loss of data..
  if (sizeof(value) > maxBytesToWrite || sizeof(value) > paramInfo.plcSize)
  {
    asynPrint(pasynUser, ASYN_TRACE_WARNING, "%s:%s: WARNING. EPICS datatype size larger than PLC datatype size (%ld vs %d bytes).\n", driverName, __func__, sizeof(value), paramInfo.plcDataType);
    paramInfo.plcDataTypeWarn = true;
  }

  // Ensure that PLC datatype and number of bytes to write match
  if (maxBytesToWrite != paramInfo.plcSize || maxBytesToWrite == 0)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types size mismatch (%s and %d bytes). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType), maxBytesToWrite);
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  // Do the write
  if (adsWriteParam(adsClientPort, paramInfo, (const void *)buffer, maxBytesToWrite) != asynSuccess)
  {
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  // Only reset if write alarm
  if (paramInfo.alarmStatus == WRITE_ALARM)
  {
    setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  }

  return asynPortDriver::writeFloat64(pasynUser, value);
}

/** Read array of a certain data type from PLC (or actually
 * paramlib,paraminfor->arrayDataBuffer, since all variables are updated
 * on-change by callbacks).
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] allowedType Allowed ads type to read.
 * \param[out] epicsDataBuffer Output buffer.
 * \param[in] nEpicsBufferBytes Output buffer size.
 * \param[out] nBytesRead Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGenericArrayRead(asynUser *pasynUser, long allowedType, void *epicsDataBuffer, size_t nEpicsBufferBytes, size_t *nBytesRead)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  int paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: pAdsParamArray NULL or index (pasynUser->reason) our of range\n", driverName, __func__);
    pasynUser->alarmStatus = READ_ALARM;
    pasynUser->alarmSeverity = INVALID_ALARM;
    return asynError;
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  // Only support same datatype as in PLC
  //  Allow LWORD/ULINT as signed INT64
  bool extendedAllowedType = (paramInfo.plcDataType == allowedType) ||
                             (paramInfo.plcDataType == ADST_UINT64 && allowedType == ADST_INT64);

  if (!extendedAllowedType)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types not compatible (%s vs %s). Read canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType), adsTypeToString(allowedType));
    setAlarmParamLock(paramInfo, READ_ALARM, INVALID_ALARM);
    return asynError;
  }

  size_t bytesToWrite = nEpicsBufferBytes;
  if (paramInfo.plcSize < nEpicsBufferBytes)
  {
    bytesToWrite = paramInfo.plcSize;
  }

  if (paramInfo.arrayDataBuffer.empty() || !epicsDataBuffer)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Buffer(s) NULL. Read canceled.\n", driverName, __func__);
    setAlarmParamLock(paramInfo, READ_ALARM, INVALID_ALARM);
    return asynError;
  }

  memcpy(epicsDataBuffer, paramInfo.arrayDataBuffer.data(), bytesToWrite);
  *nBytesRead = bytesToWrite;

  // Only reset if read alarm
  if (paramInfo.alarmStatus == READ_ALARM)
  {
    setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  }

  // update timestamp
  pasynUser->timestamp = paramInfo.epicsTimestamp;

  return asynSuccess;
}

/** Write array of a certain data type to PLC.
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] allowedType Allowed ads type to read.
 * \param[out] data Data to write.
 * \param[in] nEpicsBufferBytes Bytes to write.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGenericArrayWrite(asynUser *pasynUser, long allowedType, const void *data, size_t nEpicsBufferBytes)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  int paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: pAdsParamArray NULL or index (pasynUser->reason) our of range\n", driverName, __func__);
    pasynUser->alarmStatus = WRITE_ALARM;
    pasynUser->alarmSeverity = INVALID_ALARM;
    return asynError;
  }

  long adsClientPort = 0;
  AdsClientPortGuard adsClientPortGuard(*this, adsClientPort);
  if (isInvalidPortNumber(adsClientPort))
  {
    throw std::runtime_error(string_format(
        "%s:%s: failed to open ads client port for this thread.\n", driverName, __func__));
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  // Only support same datatype as in PLC
  //  Allow LWORD/ULINT as signed INT64
  bool extendedAllowedType = (paramInfo.plcDataType == allowedType) ||
                             (paramInfo.plcDataType == ADST_UINT64 && allowedType == ADST_INT64);

  if (!extendedAllowedType)
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: Data types not compatible (%s vs %s). Write canceled.\n", driverName, __func__, adsTypeToString(paramInfo.plcDataType), adsTypeToString(allowedType));
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  size_t bytesToWrite = nEpicsBufferBytes;
  if (paramInfo.plcSize < nEpicsBufferBytes)
  {
    bytesToWrite = paramInfo.plcSize;
  }

  // Write to ADS
  asynStatus stat = adsWriteParam(adsClientPort, paramInfo, data, bytesToWrite);
  if (stat != asynSuccess)
  {
    setAlarmParamLock(paramInfo, WRITE_ALARM, INVALID_ALARM);
    return asynError;
  }

  // copy data to buffer;
  if (!paramInfo.arrayDataBuffer.empty())
  {
    memcpy(paramInfo.arrayDataBuffer.data(), data, bytesToWrite);
  }

  // Only reset if write alarm
  if (paramInfo.alarmStatus == WRITE_ALARM)
  {
    setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  }

  return asynSuccess;
}

/** Overrides asynPortDriver::readInt8Array.
 * Reads int8Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[out] value Output data buffer.
 * \param[in] nElements Output buffer size.
 * \param[out] nIn Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  auto paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: index (pasynUser->reason) out of range\n", driverName, __func__);
    return asynError;
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  long allowedType = ADST_INT8;

  // Also allow string and bool array as int8array (special case)
  if (paramInfo.plcDataType == ADST_STRING)
  {
    allowedType = ADST_STRING;
  }
  else if (paramInfo.plcDataType == ADST_BIT)
  {
    allowedType = ADST_BIT;
  }

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsInt8), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsInt8);
  return asynSuccess;
}

/** Overrides asynPortDriver::writeInt8Array.
 * Writes int8Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  auto paramIndex = pasynUser->reason;

  if (paramIndex < 0 || (size_t)paramIndex > adsParamArray_.size())
  {
    asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: index (pasynUser->reason) out of range\n", driverName, __func__);
    return asynError;
  }

  auto &paramInfo = adsParamArray_[paramIndex];

  long allowedType = ADST_INT8;

  // Also allow string and bool array as int8array (special case)
  if (paramInfo.plcDataType == ADST_STRING)
  {
    allowedType = ADST_STRING;
  }
  else if (paramInfo.plcDataType == ADST_BIT)
  {
    allowedType = ADST_BIT;
  }

  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * sizeof(epicsInt8));
}

/** Overrides asynPortDriver::readInt16Array.
 * Reads int16Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[out] value Output data buffer.
 * \param[in] nElements Output buffer size.
 * \param[out] nIn Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::readInt16Array(asynUser *pasynUser, epicsInt16 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT16;

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsInt16), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsInt16);
  return asynSuccess;
}

/** Overrides asynPortDriver::writeInt16Array.
 * Writes int16Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeInt16Array(asynUser *pasynUser, epicsInt16 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT16;

  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * sizeof(epicsInt16));
}

/** Overrides asynPortDriver::readInt32Array.
 * Reads int32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[out] value Output data buffer.
 * \param[in] nElements Output buffer size.
 * \param[out] nIn Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::readInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT32;

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsInt32), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsInt32);
  return asynSuccess;
}

/** Overrides asynPortDriver::writeInt32Array.
 * Writes int32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT32;

  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * sizeof(epicsInt32));
}

/** Overrides asynPortDriver::readFloat32Array.
 * Reads float32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[out] value Output data buffer.
 * \param[in] nElements Output buffer size.
 * \param[out] nIn Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::readFloat32Array(asynUser *pasynUser, epicsFloat32 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_REAL32;

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsFloat32), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsFloat32);
  return asynSuccess;
}

/** Overrides asynPortDriver::writeFloat32Array.
 * Writes float32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_REAL32;
  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * sizeof(epicsFloat32));
}

asynStatus adsAsynPortDriver::readInt64Array(asynUser *pasynUser, epicsInt64 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT64;

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsInt64), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsInt64);
  return asynSuccess;
}

asynStatus adsAsynPortDriver::writeInt64Array(asynUser *pasynUser, epicsInt64 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_INT64;

  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * sizeof(epicsInt64));
}

/** Overrides asynPortDriver::readFloat64Array.
 * Reads float64Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[out] value Output data buffer.
 * \param[in] nElements Output buffer size.
 * \param[out] nIn Bytes read into buffer.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_REAL64;

  size_t nBytesRead = 0;
  asynStatus stat = adsGenericArrayRead(pasynUser, allowedType, (void *)value, nElements * sizeof(epicsFloat64), &nBytesRead);
  if (stat != asynSuccess)
  {
    return asynError;
  }
  *nIn = nBytesRead / sizeof(epicsFloat64);
  return asynSuccess;
}

/** Overrides asynPortDriver::writeFloat64Array.
 * Writes float64Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements)
{
  asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  long allowedType = ADST_REAL64;
  return adsGenericArrayWrite(pasynUser, allowedType, (const void *)value, nElements * nElements * sizeof(epicsFloat64));
}

/** Returns pasynUserSelf for use in asynPrint().
 *
 * \return pasynUserSelf
 */
asynUser *adsAsynPortDriver::getTraceAsynUser()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  return pasynUserSelf;
}

/** Get handle to symbolic plc variable.
 *
 * \param[in/out] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGetSymHandleByName(long adsClientPort, adsParamInfo &paramInfo)
{
  return adsGetSymHandleByName(adsClientPort, paramInfo, false);
}

/** Get handle to symbolic plc variable.
 *
 * \param[in/out] paramInfo Parameter information.
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGetSymHandleByName(long adsClientPort, adsParamInfo &paramInfo, bool blockErrorMsg)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = paramInfo.amsPort;

  uint32_t symbolHandle = 0;
  const long handleStatus = AdsSyncReadWriteReqEx2(adsClientPort,
                                                   &amsServer,
                                                   ADSIGRP_SYM_HNDBYNAME,
                                                   0,
                                                   sizeof(paramInfo.hSymbolicHandle),
                                                   &symbolHandle,
                                                   paramInfo.plcAdrStr.length(),
                                                   paramInfo.plcAdrStr.c_str(),
                                                   nullptr);
  if (handleStatus)
  {
    if (!blockErrorMsg)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Create handle for %s failed with: %s (0x%lx)\n",
                driverName, __func__, paramInfo.plcAdrStr.c_str(), adsErrorToString(handleStatus), handleStatus);
    }
    return asynError;
  }

  // Add handle succeded
  paramInfo.hSymbolicHandle = symbolHandle;
  paramInfo.bSymbolicHandleValid = true;

  return asynSuccess;
}

/** Register on-change callback for symbols version
 *
 * \param[in] port Structure containig Ams-port information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsAddSymbolsChangedCallback(long adsClientPort, amsPortInfo &port)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Ams-port %u.\n", driverName, __func__, port.amsPort);

        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = port.amsPort;

  AdsNotificationAttrib attrib;
  attrib.cbLength = 1;
  attrib.nTransMode = ADSTRANS_SERVERONCHA;                       // Add option
  attrib.nMaxDelay = (uint32_t)(defaultMaxDelayTime_ms_ * 10000); // 100ms
  attrib.nCycleTime = (uint32_t)(defaultSampleTime_ms_ * 10000);

  uint32_t hNotify = 0;
  long addStatus = AdsSyncAddDeviceNotificationReqEx(adsClientPort,
                                                     &amsServer,
                                                     ADSIGRP_SYM_VERSION,
                                                     0,
                                                     &attrib,
                                                     &adsSymbolsChangedCallback,
                                                     (uint32_t)port.amsPort, // Use amsPort as hUser
                                                     &hNotify);
  if (addStatus)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Add device notification failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(addStatus), addStatus);
    return asynError;
  }

  // Add was successful
  port.hCallbackNotify = hNotify;
  port.bCallbackNotifyValid = true;
  port.refreshNeeded = false;

  return asynSuccess;
}

/** Unregister on-change callback for symbols version
 *
 * \param[in] port Structure containig Ams-port information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsDelSymbolsChangedCallback(long adsClientPort, amsPortInfo &port)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = port.amsPort;

  const long delStatus = AdsSyncDelDeviceNotificationReqEx(adsClientPort, &amsServer, port.hCallbackNotify);
  port.bCallbackNotifyValid = false;
  port.hCallbackNotify = -1;

  if (delStatus)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Delete device notification failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(delStatus), delStatus);
    return asynError;
  }

  return asynSuccess;
}

/** Register on-change callback for parameter (plc-variable).
 *
 * \param[in/out] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsAddDataCallback(long adsClientPort, adsParamInfo &paramInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  uint32_t group = 0;
  uint32_t offset = 0;

  paramInfo.bCallbackNotifyValid = false;

        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = paramInfo.amsPort;

  if (paramInfo.isAdrCommand)
  { // Abs access (ADR command)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Absolute address in paramInfo not valid.\n", driverName, __func__);
      return asynError;
    }

    group = paramInfo.plcAbsAdrGroup;
    offset = paramInfo.plcAbsAdrOffset;
  }
  else
  { // Symbolic access

    // Read symbolic information if needed (to get paramInfo->plcSize)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynStatus statusInfo = adsGetSymInfoByName(adsClientPort, paramInfo);
      if (statusInfo != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymInfoByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    // Get symbolic handle if needed
    if (!paramInfo.bSymbolicHandleValid)
    {
      asynStatus statusHandle = adsGetSymHandleByName(adsClientPort, paramInfo);
      if (statusHandle != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymHandleByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    group = ADSIGRP_SYM_VALBYHND; // Access via symbolic handle stored in paramInfo->hSymbolicHandle
    offset = paramInfo.hSymbolicHandle;
  }

  AdsNotificationAttrib attrib;
  /** Length of the data that is to be passed to the callback function. */
  attrib.cbLength = paramInfo.plcSize;
  /**
   * ADSTRANS_SERVERCYCLE: The notification's callback function is invoked cyclically.
   * ADSTRANS_SERVERONCHA: The notification's callback function is only invoked when the value changes.
   */
  attrib.nTransMode = ADSTRANS_SERVERONCHA; // Add option
  /** The notification's callback function is invoked at the latest when this time has elapsed. The unit is 100 ns. */
  attrib.nMaxDelay = (uint32_t)(paramInfo.maxDelayTimeMS * 10000); // 100ms
  /** The ADS server checks whether the variable has changed after this time interval. The unit is 100 ns. */
  attrib.nCycleTime = (uint32_t)(paramInfo.sampleTimeMS * 10000);

  uint32_t hNotify = 0;
  long addStatus = AdsSyncAddDeviceNotificationReqEx(adsClientPort,
                                                     &amsServer,
                                                     group,
                                                     offset,
                                                     &attrib,
                                                     &adsDataCallback,
                                                     (uint32_t)paramInfo.paramIndex,
                                                     &hNotify);
  if (addStatus)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Add device notification failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(addStatus), addStatus);
    return asynError;
  }

  // Add was successful
  paramInfo.hCallbackNotify = hNotify;
  paramInfo.bCallbackNotifyValid = true;

  return asynSuccess;
}

/** Unregister on-change callback for parameter (plc-variable).
 *
 * \param[in/out] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsDelDataCallback(long adsClientPort, adsParamInfo &paramInfo)
{
  return adsDelDataCallback(adsClientPort, paramInfo, false);
}

/** Unregister on-change callback for parameter (plc-variable).
 *
 * \param[in/out] paramInfo Parameter information.
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsDelDataCallback(long adsClientPort, adsParamInfo &paramInfo, bool blockErrorMsg)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  paramInfo.bCallbackNotifyValid = false;

        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = paramInfo.amsPort;

  const long delStatus = AdsSyncDelDeviceNotificationReqEx(adsClientPort, &amsServer, paramInfo.hCallbackNotify);
  paramInfo.hCallbackNotify = -1;
  if (delStatus)
  {
    if (!blockErrorMsg)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Delete device notification failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(delStatus), delStatus);
    }
    return asynError;
  }

  return asynSuccess;
}

/** Get symbolic information for a plc variable.
 *
 * \param[in] amsPort Ams-port
 * \param[in] varName Symbolic name of variable.
 * \param[out] info Information structure.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGetSymInfoByName(long adsClientPort, uint16_t amsPort, const char *varName, AdsSymbolEntryExpanded &info)
{
  long errorCode = 0;
  return adsGetSymInfoByName(adsClientPort, amsPort, varName, info, &errorCode);
}

/** Get symbolic information for a plc variable.
 *
 * \param[in] amsPort Ams-port
 * \param[in] varName Symbolic name of variable.
 * \param[out] info Information structure.
 * \param[out] errorCode Ads error code.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGetSymInfoByName(long adsClientPort, uint16_t amsPort, const char *varName, AdsSymbolEntryExpanded &info, long *errorCode)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Variable name: %s, amsPort: %d.\n", driverName, __func__, varName, (int)amsPort);

  if (!varName)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Info struct or varName NULL.\n", driverName, __func__);
    return asynError;
  }

  uint32_t bytesRead = 0;
        AmsAddr amsServer;
        amsServer.netId = remoteNetId_;
        amsServer.port = amsPort;

  auto it = adsSymbolMap_.find(varName);
  if (it == adsSymbolMap_.end())
  {
    AdsSymbolEntryAccess infoAccess;
    printf("%s:%s: did not find %s in the symbol map.\n", driverName, __func__, varName);
    const long infoStatus = AdsSyncReadWriteReqEx2(adsClientPort,
                                                   &amsServer,
                                                   ADSIGRP_SYM_INFOBYNAMEEX,
                                                   0,
                                                   sizeof(AdsSymbolEntry),
                                                   &infoAccess,
                                                   strlen(varName),
                                                   varName,
                                                   &bytesRead);
    *errorCode = infoStatus;

    if (infoStatus)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Get symbolic information failed for %s with: %s (0x%lx)\n", driverName, __func__, varName, adsErrorToString(infoStatus), infoStatus);
      return asynError;
    }

    info.entryLength = infoAccess.entryLength;
    info.iGroup = infoAccess.iGroup;
    info.iOffs = infoAccess.iOffs;
    info.size = infoAccess.size;
    info.dataType = infoAccess.dataType;
    info.flags = infoAccess.flags;
    info.nameLength = infoAccess.nameLength;
    info.typeLength = infoAccess.typeLength;
    info.commentLength = infoAccess.commentLength;
    
    info.name = infoAccess.name();
    info.type = infoAccess.type();
    info.comment = infoAccess.comment();
  }
  else
  {
    printf("%s:%s: found %s in the symbol map.\n", driverName, __func__, varName);
    // const long infoStatus = AdsSyncReadWriteReqEx2(adsClientPort,
    //                                                &amsServer,
    //                                                ADSIGRP_SYM_INFOBYNAMEEX,
    //                                                0,
    //                                                sizeof(AdsSymbolEntry),
    //                                                &info,
    //                                                strlen(varName),
    //                                                varName,
    //                                                &bytesRead);
    // *errorCode = infoStatus;

    // if (infoStatus)
    // {
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Get symbolic information failed for %s with: %s (0x%lx)\n", driverName, __func__, varName, adsErrorToString(infoStatus), infoStatus);
    //   return asynError;
    // }

    info = *it->second;

    // if (info.entryLen != it->second->entryLength)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong entry length. should be: %u is: %u\n", driverName, __func__, varName, info.entryLen, it->second->entryLength);
    // if (info.iGroup != it->second->iGroup)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong group. should be: %u is: %u\n", driverName, __func__, varName, info.iGroup, it->second->iGroup);
    // if (info.iOffset != it->second->iOffs)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong offset. should be: %u is: %u\n", driverName, __func__, varName, info.iOffset, it->second->iOffs);
    // if (info.size != it->second->size)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong size. should be: %u is: %u\n", driverName, __func__, varName, info.size, it->second->size);
    // if (info.dataType != it->second->dataType)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong datatype. should be: %u is: %u\n", driverName, __func__, varName, info.dataType, it->second->dataType);
    // if (info.flags != it->second->flags)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong flags. should be: %u is: %u\n", driverName, __func__, varName, info.flags, it->second->flags);
    // if (info.nameLength != it->second->nameLength)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong nameLength. should be: %u is: %u\n", driverName, __func__, varName, info.nameLength, it->second->nameLength);
    // if (info.typeLength != it->second->typeLength)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong typeLength. should be: %u is: %u\n", driverName, __func__, varName, info.typeLength, it->second->typeLength);
    // if (info.commentLength != it->second->commentLength)
    //   asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: %s: detected wrong commentLength. should be: %u is: %u\n", driverName, __func__, varName, info.commentLength, it->second->commentLength);
  }

  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Symbolic information\n");
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "SymEntrylength: %d\n", info.entryLength);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "idxGroup: 0x%x\n", info.iGroup);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "idxOffset: 0x%x\n", info.iOffs);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "ByteSize: %d\n", info.size);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "adsDataType: %d\n", info.dataType);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Flags: %d\n", info.flags);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Name length: %d\n", info.nameLength);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Type length: %d\n", info.typeLength);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Type length: %d\n", info.commentLength);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Variable name: %s\n", info.name.c_str());
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Data type: %s\n", info.type.c_str());
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Comment: %s\n", info.comment.c_str());

  return asynSuccess;
}

/** Get symbolic information for a plc variable.
 *
 * \param[in/out] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsGetSymInfoByName(long adsClientPort, adsParamInfo &paramInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  AdsSymbolEntryExpanded infoStruct;

  asynStatus stat = adsGetSymInfoByName(adsClientPort, paramInfo.amsPort, paramInfo.plcAdrStr.c_str(), infoStruct);
  if (stat)
  {
    return asynError;
  }

  // fill paramInfo data structure
  paramInfo.plcAbsAdrGroup = infoStruct.iGroup;
  paramInfo.plcAbsAdrOffset = infoStruct.iOffs;
  paramInfo.plcSize = infoStruct.size;
  paramInfo.plcDataType = infoStruct.dataType;
  paramInfo.plcAbsAdrValid = true;

  return asynSuccess;
}

/** Connect to ads router (TwinCAT system).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsConnect(long adsClientPort)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  if (!routeEstablished_)
  {
    adsDelRoute();
    asynStatus stat = adsAddRoute();

    if (stat != asynSuccess)
    {
      adsDelRoute();
      return asynError;
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
              "%s:%s: successfully added route to ip = %s, netid = %s.\n",
              driverName, __func__, ipaddr_.c_str(), amsaddr_.c_str());
  }

  // Update timeout
  uint32_t defaultTimeout = 0;
  long status = AdsSyncGetTimeoutEx(adsClientPort, &defaultTimeout);
  if (status)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: AdsSyncGetTimeoutEx failed with: %s (0x%lx).\n", driverName, __func__, adsErrorToString(status), status);
    return asynError;
  }
  if (defaultTimeout == adsTimeout_ms_)
    return asynSuccess;

  status = AdsSyncSetTimeoutEx(adsClientPort, (uint32_t)adsTimeout_ms_);
  if (status)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: AdsSyncSetTimeoutEx failed with: %s (0x%lx).\n", driverName, __func__, adsErrorToString(status), status);
    return asynError;
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Updated ADS sync time out from %u to %u.\n", driverName, __func__, defaultTimeout, adsTimeout_ms_);

  return asynSuccess;
}

/** Read Ams port version information
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadVersion(long adsClientPort, amsPortInfo &port)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Ams-port %u\n", driverName, __func__, port.amsPort);

  AmsAddr amsServer;
  AdsVersion version;
  char devName[255];
  amsServer.netId = remoteNetId_;
  amsServer.port = port.amsPort;

  long status = AdsSyncReadDeviceInfoReqEx(adsClientPort, &amsServer, devName, &version);
  if (status)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: AdsSyncReadDeviceInfoReqEx failed with: %s (0x%lx).\n", driverName, __func__, adsErrorToString(status), status);
    return asynError;
  }

  port.version = version;
  strncpy(port.devName, devName, sizeof(port.devName));
  return asynSuccess;
}

/** Release handle to symbolic variable (in TwinCAT plc)
 *
 * \param[in/out] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReleaseSymbolicHandle(long adsClientPort, adsParamInfo &paramInfo)
{
  return adsReleaseSymbolicHandle(adsClientPort, paramInfo, false);
}

/** Release handle to symbolic variable (in TwinCAT plc)
 *
 * \param[in/out] paramInfo Parameter information.
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReleaseSymbolicHandle(long adsClientPort, adsParamInfo &paramInfo, bool blockErrorMsg)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  paramInfo.bSymbolicHandleValid = false;

  AmsAddr amsServer;
  amsServer.netId = remoteNetId_;
  amsServer.port = paramInfo.amsPort;

  const long releaseStatus = AdsSyncWriteReqEx(adsClientPort, &amsServer, ADSIGRP_SYM_RELEASEHND, 0, sizeof(paramInfo.hSymbolicHandle), &paramInfo.hSymbolicHandle);
  paramInfo.hSymbolicHandle = -1;
  if (releaseStatus && !blockErrorMsg)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: Release of handle 0x%x failed with: %s (0x%lx)\n",
              driverName, __func__, paramInfo.hSymbolicHandle, adsErrorToString(releaseStatus), releaseStatus);
    return asynError;
  }

  return asynSuccess;
}

/** Write value to variable in TwinCAT.
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] binaryBuffer Data to write.
 * \param[in] bytesToWrite Bytes to write.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsWriteParam(long adsClientPort, adsParamInfo &paramInfo, const void *binaryBuffer, uint32_t bytesToWrite)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  // Calculate consumed time by this method
  struct timeval start, end;
  long secs_used, micros_used;
  gettimeofday(&start, NULL);

  uint32_t group = 0;
  uint32_t offset = 0;

  AmsAddr amsServer;
  amsServer.netId = remoteNetId_;
  amsServer.port = paramInfo.amsPort;

  if (paramInfo.isAdrCommand)
  { // Abs access (ADR command)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Absolute address in paramInfo not valid.\n", driverName, __func__);
      return asynError;
    }

    group = paramInfo.plcAbsAdrGroup;
    offset = paramInfo.plcAbsAdrOffset;
  }
  else
  {
    // Symbolic access
    // Read symbolic information if needed (to get paramInfo->plcSize)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynStatus statusInfo = adsGetSymInfoByName(adsClientPort, paramInfo);
      if (statusInfo == asynError)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymInfoByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    // Get symbolic handle if needed
    if (!paramInfo.bSymbolicHandleValid)
    {
      asynStatus statusHandle = adsGetSymHandleByName(adsClientPort, paramInfo);
      if (statusHandle != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymHandleByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    group = ADSIGRP_SYM_VALBYHND; // Access via symbolic handle stored in paramInfo->hSymbolicHandle
    offset = paramInfo.hSymbolicHandle;
  }
  long writeStatus = AdsSyncWriteReqEx(adsClientPort,
                                       &amsServer,
                                       group,
                                       offset,
                                       paramInfo.plcSize,
                                       binaryBuffer);
  if (writeStatus)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ADS write failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(writeStatus), writeStatus);
    return asynError;
  }

  gettimeofday(&end, NULL);
  secs_used = (end.tv_sec - start.tv_sec); // avoid overflow by subtracting first
  micros_used = ((secs_used * 1000000) + end.tv_usec) - (start.tv_usec);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s: ADS write: micros used: 0x%lx\n", driverName, __func__, micros_used);

  return asynSuccess;
}

/** Read value of variable in TwinCAT.
 *
 * \param[in] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadParam(long adsClientPort, adsParamInfo &paramInfo)
{
  long notused = 0;
  return adsReadParam(adsClientPort, paramInfo, notused, 1);
}

/** Read value of variable in TwinCAT.
 *
 * \param[in] paramInfo Parameter information.
 * \param[out] error Error code.
 * \param[in] updateAsynPar Update asyn parameter.
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadParam(long adsClientPort, adsParamInfo &paramInfo, long &error, int updateAsynPar)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  uint32_t group = 0;
  uint32_t offset = 0;
  error = 0;

  AmsAddr amsServer;
  amsServer.netId = remoteNetId_;
  amsServer.port = paramInfo.amsPort;

  if (paramInfo.isAdrCommand)
  { // Abs access (ADR command)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Absolute address in paramInfo not valid.\n", driverName, __func__);
      return asynError;
    }

    group = paramInfo.plcAbsAdrGroup;
    offset = paramInfo.plcAbsAdrOffset;
  }
  else
  { // Symbolic access

    // Read symbolic information if needed (to get paramInfo->plcSize)
    if (!paramInfo.plcAbsAdrValid)
    {
      asynStatus statusInfo = adsGetSymInfoByName(adsClientPort, paramInfo);
      if (statusInfo == asynError)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymInfoByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    // Get symbolic handle if needed
    if (!paramInfo.bSymbolicHandleValid)
    {
      asynStatus statusHandle = adsGetSymHandleByName(adsClientPort, paramInfo);
      if (statusHandle != asynSuccess)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: adsGetSymHandleByName failed.\n", driverName, __func__);
        return asynError;
      }
    }

    group = ADSIGRP_SYM_VALBYHND; // Access via symbolic handle stored in paramInfo->hSymbolicHandle
    offset = paramInfo.hSymbolicHandle;
  }

  char *data = new char[paramInfo.plcSize];
  uint32_t bytesRead = 0;
  error = AdsSyncReadReqEx2(adsClientPort,
                            &amsServer,
                            group,
                            offset,
                            paramInfo.plcSize,
                            (void *)data,
                            &bytesRead);
  if (error)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: AdsSyncReadReqEx2 failed: %s (%lu).\n",
              driverName, __func__, adsErrorToString(error), error);
    return asynError;
  }

  if (bytesRead != paramInfo.plcSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: Read bytes differ from parameter plc size (%u vs %u).\n",
              driverName, __func__, bytesRead, paramInfo.plcSize);
    return asynError;
  }

  // No timestamp available
  paramInfo.plcTimeStampRaw = 0;
  paramInfo.firstReadDone = true;

  asynStatus stat = asynSuccess;
  if (updateAsynPar)
  {
    stat = adsUpdateParameterLock(paramInfo, (const void *)data, bytesRead);
  }

  return stat;
}

/** Read state of amsport in TwinCAT
 *
 * \param[in] amsport Ams-prot.
 * \param[out] adsState State of ams-port (running, invalid, config..).
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 *
 * Thread safe.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadStateLock(long adsClientPort, uint16_t amsport, uint16_t &adsState, bool blockErrorMsg)
{
  asynStatus stat;
  lock();
  long error = 0;
  stat = adsReadState(adsClientPort, amsport, adsState, blockErrorMsg, error);
  unlock();
  return stat;
}

/** Read state of amsport in TwinCAT
 *
 * \param[in] amsport Ams-prot.
 * \param[out] adsState State of ams-port (running, invalid, config..).
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 * \param[out] error Error code.
 *
 * Thread safe.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadStateLock(long adsClientPort, uint16_t amsport, uint16_t &adsState, bool blockErrorMsg, long &error)
{
  asynStatus stat;
  // If we are locking the mutex then we must be using the shared ads client port.
  stat = adsReadState(adsClientPort, amsport, adsState, blockErrorMsg, error);
  return stat;
}

/** Read state of default amsport in TwinCAT
 *
 * \param[out] adsState State of ams-port (running, invalid, config..).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadState(long adsClientPort, uint16_t &adsState)
{
  long error = 0;
  return adsReadState(adsClientPort, amsportDefault_, adsState, false, error);
}

/** Read state of amsport in TwinCAT
 *
 * \param[in] adsClientPort specific ads client port assigned to the calling thread.
 * \param[in] amsport Ams-port.
 * \param[out] adsState State of ams-port (running, invalid, config..).
 * \param[in] blockErrorMsg Suppress error messages
 *            (used while trying to reconnect to avoid alot of error messages).
 * \param[out] error Error code.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsReadState(long adsClientPort, uint16_t amsport, uint16_t &adsState, bool blockErrorMsg, long &error)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  AmsAddr amsServer;
  amsServer.netId = remoteNetId_;
  amsServer.port = amsport;

  uint16_t devState;
  error = AdsSyncReadStateReqEx(adsClientPort, &amsServer, &adsState, &devState);
  if (error)
  {
    if (!blockErrorMsg)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ADS read state failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(error), error);
    }
    return asynError;
  }

  return asynSuccess;
}

/** Get parameter table size (max allowed parameter count).
 * \param[in] amsport Ams-port.
 * \param[in] adsState State of ams-port (running, invalid, config..).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsWriteState(long adsClientPort, uint16_t amsport, uint16_t adsState)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: adsState = %s (%u)\n", driverName, __func__, adsStateToString(adsState), adsState);

  void *pData = NULL;
  AmsAddr amsServer;
  amsServer.netId = remoteNetId_;
  amsServer.port = amsport;
  const long status = AdsSyncWriteControlReqEx(adsClientPort, &amsServer, adsState, 0, 0, pData);
  if (status)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: ADS write state failed with: %s (0x%lx)\n", driverName, __func__, adsErrorToString(status), status);
    return asynError;
  }

  return asynSuccess;
}

/** Get parameter table size (max allowed parameter count).
 *
 * \return Aysn -parameter table size.
 */
int adsAsynPortDriver::getParamTableSize()
{
  return adsParamArray_.size();
}

/** Get parameter info struct for a certain index/reason (pasynUser->reason).
 *
 * \param[in] index index/reason (pasynUser->reason).
 *
 * \return Parameter info structure.
 */
adsParamInfo *adsAsynPortDriver::getAdsParamInfo(int index)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Get paramInfo for index: %d\n", driverName, __func__, index);

  if (index < 0 || (size_t)index >= adsParamArray_.size())
  {
    return nullptr;
  }
  return &adsParamArray_[index];
}

/** Get current parameter count.
 *
 * \return Current parameter count.
 */
int adsAsynPortDriver::getAdsParamCount()
{
  return adsParamArray_.size();
}

/** Update timestamp of parameter.
 *
 * \param[in] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 *
 * Refreshes and sets timestamp depending on time source (PLC or EPICS).
 */
asynStatus adsAsynPortDriver::refreshParamTime(adsParamInfo &paramInfo)
{
  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: plcTime %lu.\n", driverName, __func__, paramInfo.plcTimeStampRaw);

  // Convert plc timeStamp (windows format) to epicsTimeStamp
  if (windowsToEpicsTimeStamp(paramInfo.plcTimeStampRaw, &paramInfo.plcTimeStamp))
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: windowsToEpicsTimeStamp() failed.\n", driverName, __func__);
    return asynError;
  }

  epicsTimeStamp ts;

  // Update time stamp
  if (paramInfo.timeBase == ADS_TIME_BASE_EPICS || paramInfo.plcTimeStampRaw == 0)
  {
    if (updateTimeStamp() != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: updateTimeStamp() failed.\n", driverName, __func__);
      return asynError;
    }
  }
  else
  {
    // ADS_TIME_BASE_PLC
    if (setTimeStamp(&paramInfo.plcTimeStamp) != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: updateTimeStamp() failed.\n", driverName, __func__);
      return asynError;
    }
  }

  if (getTimeStamp(&ts) != asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: getTimeStamp() failed.\n", driverName, __func__);
    return asynError;
  }

  paramInfo.epicsTimestamp = ts;

  return asynSuccess;
}

/** Update asyn parameter or callback (for arrays).
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] data Data to write to parameter (or callback to EPICS).
 *
 * \return asynSuccess or asynError.
 *
 * Thread safe.
 */
asynStatus adsAsynPortDriver::adsUpdateParameterLock(adsParamInfo &paramInfo, const void *data)
{
  lock();
  asynStatus stat = adsUpdateParameter(paramInfo, data);
  unlock();
  return stat;
}

/** Update asyn parameter or callback (for arrays).
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] data Data to write to parameter (or callback to EPICS).
 * \param[in] dataSize Size of data to write.
 *
 * \return asynSuccess or asynError.
 *
 * Thread safe.
 */
asynStatus adsAsynPortDriver::adsUpdateParameterLock(adsParamInfo &paramInfo, const void *data, size_t dataSize)
{
  lock();
  asynStatus stat = adsUpdateParameter(paramInfo, data, dataSize);
  unlock();
  return stat;
}

/** Update asyn parameter or callback (for arrays).
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] data Data to write to parameter (or callback to EPICS).
 *
 * \return asynSuccess or asynError.
 *
 */
asynStatus adsAsynPortDriver::adsUpdateParameter(adsParamInfo &paramInfo, const void *data)
{
  return adsUpdateParameter(paramInfo, data, paramInfo.lastCallbackSize);
}

/** Update asyn parameter or callback (for arrays).
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] data Data to write to parameter (or callback to EPICS).
 * \param[in] dataSize Size of data to write.
 *
 * \return asynSuccess or asynError.
 *
 */
asynStatus adsAsynPortDriver::adsUpdateParameter(adsParamInfo &paramInfo, const void *data, size_t dataSize)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  paramInfo.callbackPending = true;

  if (!data)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: data NULL.\n", driverName, __func__);
    return asynError;
  }

  if (refreshParamTime(paramInfo) != asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: refreshParamTime() failed.\n", driverName, __func__);
    return asynError;
  }

  asynStatus ret = asynError;

  // Ensure check if array
  if (paramInfo.plcDataIsArray)
  {
    if (paramInfo.arrayDataBuffer.empty())
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Array but buffer is NULL.\n", driverName, __func__);
      return asynError;
    }
    // Copy data to param buffer
    memcpy(paramInfo.arrayDataBuffer.data(), data, paramInfo.lastCallbackSize);
  }

  switch (paramInfo.plcDataType)
  {
  case ADST_INT8:
    int8_t *ADST_INT8Var;
    ADST_INT8Var = ((int8_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_INT8Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_INT8Var));
      break;
    case asynParamInt8Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  case ADST_INT16:
    int16_t *ADST_INT16Var;
    ADST_INT16Var = ((int16_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_INT16Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_INT16Var));
      break;
    case asynParamInt16Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_INT32:
    int32_t *ADST_INT32Var;
    ADST_INT32Var = ((int32_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_INT32Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_INT32Var));
      break;
    case asynParamInt32Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_INT64:
    int64_t *ADST_INT64Var;
    ADST_INT64Var = ((int64_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_INT64Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_INT64Var));
      break;
    case asynParamInt64:
      ret = setInteger64Param(paramInfo.paramIndex, *ADST_INT64Var);
      break;
    // No 64 bit uint array callback type (also no 64bit uint in EPICS)
    case asynParamInt64Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_UINT8:
    uint8_t *ADST_UINT8Var;
    ADST_UINT8Var = ((uint8_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_UINT8Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_UINT8Var));
      break;
    // Arrays of unsigned not supported
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_UINT16:
    uint16_t *ADST_UINT16Var;
    ADST_UINT16Var = ((uint16_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_UINT16Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_UINT16Var));
      break;
    // Arrays of unsigned not supported
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_UINT32:
    uint32_t *ADST_UINT32Var;
    ADST_UINT32Var = ((uint32_t *)data);
    // Asyn types
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_UINT32Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_UINT32Var));
      break;
    // Arrays of unsigned not supported
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_UINT64:
    uint64_t *ADST_UINT64Var;
    ADST_UINT64Var = ((uint64_t *)data);
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_UINT64Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_UINT64Var));
      break;
    case asynParamInt64:
      ret = setInteger64Param(paramInfo.paramIndex, (epicsInt64)(*ADST_UINT64Var));
      break;
    // No 64 bit uint array callback type (also no 64bit uint in EPICS)
    case asynParamInt64Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_REAL32:
    float *ADST_REAL32Var;
    ADST_REAL32Var = ((float *)data);
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_REAL32Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_REAL32Var));
      break;
    case asynParamFloat32Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_REAL64:
    double *ADST_REAL64Var;
    ADST_REAL64Var = ((double *)data);
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_REAL64Var));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_REAL64Var));
      break;
    case asynParamFloat64Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  case ADST_BIT:
    int8_t *ADST_BitVar;
    ADST_BitVar = ((int8_t *)data);
    switch (paramInfo.asynType)
    {
    case asynParamInt32:
      ret = setIntegerParam(paramInfo.paramIndex, (int)(*ADST_BitVar));
      break;
    case asynParamFloat64:
      ret = setDoubleParam(paramInfo.paramIndex, (double)(*ADST_BitVar));
      break;
    case asynParamInt8Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_STRING:
    switch (paramInfo.asynType)
    {
    case asynParamInt8Array:
      // handled in fireCallbacks()
      ret = asynSuccess;
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  default:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
              driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
    return asynError;
    break;
  }

  if (ret != asynSuccess)
  {
    return ret;
  }

  ret = setAlarmParamLock(paramInfo, NO_ALARM, NO_ALARM);
  if (ret != asynSuccess)
  {
    return ret;
  }

  return asynSuccess;
}

/** Call callbacks for all parameters.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::fireAllCallbacksLock()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  lock();
  for (auto &paramInfo : adsParamArray_)
  {
    if (!paramInfo.callbackPending)
      continue;
    fireCallbacks(paramInfo);
  }
  unlock();
  return asynSuccess;
}

/** Call callbacks for a parameter.
 *
 * \param[in] paramInfo Parameter information.
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::fireCallbacks(adsParamInfo &paramInfo)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  paramInfo.callbackPending = false;

  if (paramInfo.lastCallbackSize <= 0)
  {
    return asynSuccess;
  }

  if (!paramInfo.plcDataIsArray)
  {
    std::lock_guard<std::mutex> lockGuardCallbacks(callbacksMutex_);
    return callParamCallbacks();
  }

  asynStatus ret = asynError;

  // Array
  switch (paramInfo.plcDataType)
  {
  case ADST_INT8:
    switch (paramInfo.asynType)
    {
    case asynParamInt8Array:
      ret = doCallbacksInt8Array((epicsInt8 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsInt8), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  case ADST_INT16:
    switch (paramInfo.asynType)
    {
    case asynParamInt16Array:
      ret = doCallbacksInt16Array((epicsInt16 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsInt16), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_INT32:
    switch (paramInfo.asynType)
    {
    case asynParamInt32Array:
      ret = doCallbacksInt32Array((epicsInt32 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsInt32), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_INT64:
    switch (paramInfo.asynType)
    {
    case asynParamInt64Array:
      ret = doCallbacksInt64Array((epicsInt64 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsInt64), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
    // No 64 bit uint array callback type -> cast into int64_t and use doCallbacksInt64Array
  case ADST_UINT64:
    switch (paramInfo.asynType)
    {
    case asynParamInt64Array:
      ret = doCallbacksInt64Array((epicsInt64 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsUInt64), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_REAL32:
    switch (paramInfo.asynType)
    {
    case asynParamFloat32Array:
      ret = doCallbacksFloat32Array((epicsFloat32 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsFloat32), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  case ADST_REAL64:
    switch (paramInfo.asynType)
    {
    case asynParamFloat64Array:
      ret = doCallbacksFloat64Array((epicsFloat64 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsFloat64), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  case ADST_BIT:
    switch (paramInfo.asynType)
    {
    case asynParamInt8Array:
      ret = doCallbacksInt8Array((epicsInt8 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize / sizeof(epicsInt8), paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;
  case ADST_STRING:
    switch (paramInfo.asynType)
    {
    case asynParamInt8Array:
      ret = doCallbacksInt8Array((epicsInt8 *)paramInfo.arrayDataBuffer.data(), paramInfo.lastCallbackSize, paramInfo.paramIndex, paramInfo.asynAddr);
      break;
    default:
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
                driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
      return asynError;
      break;
    }
    break;

  default:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: Type combination not supported. PLC type = %s, ASYN type= %s\n",
              driverName, __func__, adsTypeToString(paramInfo.plcDataType), asynTypeToString(paramInfo.asynType));
    return asynError;
    break;
  }
  return ret;
}

asynStatus adsAsynPortDriver::setAlarmParamLock(adsParamInfo &paramInfo, int alarm, int severity)
{
  lock();
  return setAlarmParam(paramInfo, alarm, severity);
  unlock();
}

/** Set parameter alarm state.
 *
 * \param[in] paramInfo Parameter information.
 * \param[in] alarm Alarm type (EPICS def).
 * \param[in] severity Alarm severity (EPICS def).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::setAlarmParam(adsParamInfo &paramInfo, int alarm, int severity)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex);

  asynStatus stat;
  int oldAlarmStatus = 0;
  stat = getParamAlarmStatus(paramInfo.paramIndex, &oldAlarmStatus);
  if (stat != asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: getParamAlarmStatus failed for parameter %s (%d).\n",
              driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
    return asynError;
  }

  if (oldAlarmStatus != alarm)
  {
    stat = setParamAlarmStatus(paramInfo.paramIndex, alarm);
    if (stat != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed set alarm status for parameter %s (%d).\n",
                driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
      return asynError;
    }
    paramInfo.alarmStatus = alarm;
  }

  int oldAlarmSeverity = 0;
  stat = getParamAlarmSeverity(paramInfo.paramIndex, &oldAlarmSeverity);
  if (stat != asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: getParamAlarmStatus failed for parameter %s (%d).\n",
              driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
    return asynError;
  }

  if (oldAlarmSeverity != severity)
  {
    stat = setParamAlarmSeverity(paramInfo.paramIndex, severity);
    if (stat != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Failed set alarm severity for parameter %s (%d).\n",
                driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
      return asynError;
    }
    paramInfo.alarmSeverity = severity;
  }

  return stat;
}

/** Set parameter alarm state.
 *
 * \param[in] amsPort Ams-port.
 * \param[in] alarm Alarm type (EPICS def).
 * \param[in] severity Alarm severity (EPICS def).
 *
 * \return asynSuccess or asynError.
 *
 * Thread safe.
 */
asynStatus adsAsynPortDriver::setAlarmPortLock(uint16_t amsPort, int alarm, int severity)
{
  asynStatus stat;
  lock();
  stat = setAlarmPort(amsPort, alarm, severity);
  unlock();
  return stat;
}

/** Set alarm for all parameter on a ams-port.
 *
 * \param[in] amsPort Ams-port.
 * \param[in] alarm Alarm type (EPICS def).
 * \param[in] severity Alarm severity (EPICS def).
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::setAlarmPort(uint16_t amsPort, int alarm, int severity)
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s:\n", driverName, __func__);

  for (auto &paramInfo : adsParamArray_)
  {
    if (paramInfo.amsPort == amsPort)
    {
      if (setAlarmParamLock(paramInfo, alarm, severity) != asynSuccess)
      {
        return asynError;
      }
    }
  }
  return asynSuccess;
}

/** Delete ads route
 *
 * \return asynSuccess or asynError.
 */
asynStatus adsAsynPortDriver::adsDelRoute()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: deleting route.\n", driverName, __func__);

  std::lock_guard<std::mutex> lockGuard(adsAddDelRouteMutex_);

  routeEstablished_ = false;
  AdsDelRoute(remoteNetId_);

  return asynSuccess;
}

/** Add ads route
 *
 * \return asynSuccess or asynError.
 *
 */
asynStatus adsAsynPortDriver::adsAddRoute()
{
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: adding route.\n", driverName, __func__);

  std::lock_guard<std::mutex> lockGuard(adsAddDelRouteMutex_);

  const long addRouteStatus = AdsAddRoute(remoteNetId_, ipaddr_.c_str());
  if (addRouteStatus)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s: Adding ADS route failed with: %s (0x%lx).\n", driverName, __func__, adsErrorToString(addRouteStatus), addRouteStatus);
    return asynError;
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_INFO, "%s:%s: Adding ADS route succeeded.\n", driverName, __func__);

  routeEstablished_ = true;
  return asynSuccess;
}

/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C"
{
  asynUser *pPrintOutAsynUser;

  static void printHelp()
  {
    printf("\n");
    printf(" EPICS integration of TwinCAT PLC:s by ADS communication.\n");
    printf("\n");
    printf(" Command usage:\n");
    printf(" adsAsynPortDriverConfigure(<Asyn port name>,\n");
    printf("                            <IP address of PLC>,\n");
    printf("                            <AMS address of PLC>,\n");
    printf("                            <Default AMS port>,\n");
    printf("                            <Maximum parameter count>,\n");
    printf("                            <Asyn priority>,\n");
    printf("                            <Asyn disable auto connect>,\n");
    printf("                            <Default sample time> [ms],\n");
    printf("                            <Default max delay time [ms]>,\n");
    printf("                            <ADS command timeout [ms]>,\n");
    printf("                            <Default time source>)\n");
    printf("\n");
    printf(" Example configuration:\n");
    printf(" 0. Asyn port name                             : \"ADS_1\"\n");
    printf(" 1. IP                                         : \"192.168.88.44\"\n");
    printf(" 2. AMS of plc                                 : \"192.168.88.44.1.1\"\n");
    printf(" 3. Default ams port                           : 851 for plc 1, 852 plc 2 ...\n");
    printf(" 4. Parameter table size (max parameters)      : 1000 example\n");
    printf(" 5. priority                                   : 0\n");
    printf(" 6. disable auto connect                       : 0 (autoconnect enabled)\n");
    printf(" 7. default sample time ms                     : 500 (check if variable changed each 500ms)\n");
    printf(" 8. max delay time ms (buffer time in plc)     : 1000 (if changed, send data atleast each 1000ms or faster if send buffer is full)\n");
    printf(" 9. ADS command timeout in ms                 : 1000 (timeout for adsLib commands)\n");
    printf(" 10. default time source (PLC=0,EPICS=1).      : 0 (PLC) NOTE: record TSE field need to be set to -2 for timestamp in asyn (field(TSE, -2))\n");
    printf("\n");
    printf(" Resulting adsAsynPortDriverConfigure() command: \n");
    printf(" adsAsynPortDriverConfigure(\"ADS_1\",\"192.168.88.44\",\"192.168.88.44.1.1\",851,1000,0,0,50,100,1000,0)\n");
    printf("\n");
    printf("\n");
    printf(" NOTE: An ADS route needs to be added to the TwinCAT router of the controller/PLC:\n");
    printf("       1. \"TwinCAT->System->Routes->Static Routes\": Press \"Add\" button.\n");
    printf("       2. \"Route Name (Target)\": Enter name of EPICS machine.\n");
    printf("       3. \"AMSNetId\": Enter IP of EPICS machine. Add \".1.1\" in the end (x.x.x.x.1.1).\n");
    printf("       4. \"Address Info\": Enter IP of EPICS machine (x.x.x.x).\n");
    printf("       5. Choose \"IP Address\" checkbox.\n");
    printf("       6. Choose \"Remote Route\"->\"None\" checkbox.\n");
    printf("       7. Press \"Add Route\" button.\n");
    printf("       8. Close \"Add Route Dialog\".\n");
    printf("       9. Ensure that the route was successfully added in the \"Static Routes\" list.\n");
    printf("\n");

    return;
  }
  /*
   * Configure and register
   */
  epicsShareFunc int
  adsAsynPortDriverConfigure(const char *portName,
                             const char *ipaddr,
                             const char *amsaddr,
                             unsigned int amsport,
                             unsigned int asynParamTableSize,
                             unsigned int priority,
                             int noAutoConnect,
                             int defaultSampleTimeMS,
                             int maxDelayTimeMS,
                             int adsTimeoutMS,
                             int defaultTimeSource)
  {

    if (!portName)
    {
      printHelp();
      return -1;
    }

    if (strlen(portName) == 0 || strcmp(portName, "-h") == 0)
    {
      printHelp();
      return -1;
    }

    if (!ipaddr)
    {
      printf("adsAsynPortDriverConfigure bad ipaddr: %s\n", ipaddr ? ipaddr : "");
      return -1;
    }
    if (!amsaddr)
    {
      printf("adsAsynPortDriverConfigure bad amsaddr: %s\n", amsaddr ? amsaddr : "");
      return -1;
    }
    if (defaultSampleTimeMS < 0)
    {
      printf("adsAsynPortDriverConfigure bad defaultSampleTimeMS: %dms. Standard value of 100ms will be used.\n", defaultSampleTimeMS);
      defaultSampleTimeMS = 100;
    }

    if (maxDelayTimeMS < 0)
    {
      printf("adsAsynPortDriverConfigure bad maxDelayTimeMS: %dms. Standard value of 500ms will be used.\n", maxDelayTimeMS);
      maxDelayTimeMS = 500;
    }

    if (adsTimeoutMS < 0)
    {
      printf("adsAsynPortDriverConfigure bad adsTimeoutMS: %dms. Standard value of 2000ms will be used.\n", adsTimeoutMS);
      adsTimeoutMS = 2000;
    }

    if (defaultTimeSource < 0 || defaultTimeSource >= ADS_TIME_BASE_MAX)
    {
      printf("adsAsynPortDriverConfigure bad default time source: %d. PLC time stamps will be used. Valid options are: PLC=%d and EPICS=%d.\n", defaultTimeSource, (int)ADS_TIME_BASE_PLC, (int)ADS_TIME_BASE_EPICS);
      defaultTimeSource = ADS_TIME_BASE_PLC;
    }

    printf("Constructing adsAsynPortDriver...\n");
    adsAsynPortObj.reset(new adsAsynPortDriver(portName,
                                               ipaddr,
                                               amsaddr,
                                               amsport,
                                               asynParamTableSize,
                                               priority,
                                               noAutoConnect == 0,
                                               defaultSampleTimeMS,
                                               maxDelayTimeMS,
                                               adsTimeoutMS,
                                               (ADSTIMESOURCE)defaultTimeSource));
    printf("adsAsynPortDriver constructed.\n");
    if (adsAsynPortObj)
    {
      asynUser *traceUser = adsAsynPortObj->getTraceAsynUser();
      if (!traceUser)
      {
        printf("adsAsynPortDriverConfigure: ERROR: Failed to retrieve asynUser for trace. \n");
        return (asynError);
      }
      pPrintOutAsynUser = traceUser;
    }

    initHook();

    return asynSuccess;
  }

  /*
   * IOC shell command registration
   */
  static const iocshArg adsAsynPortDriverConfigureArg0 = {"port name", iocshArgString};
  static const iocshArg adsAsynPortDriverConfigureArg1 = {"ip-addr", iocshArgString};
  static const iocshArg adsAsynPortDriverConfigureArg2 = {"ams-addr", iocshArgString};
  static const iocshArg adsAsynPortDriverConfigureArg3 = {"default-ams-port", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg4 = {"asyn param table size", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg5 = {"priority", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg6 = {"disable auto-connect", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg7 = {"default sample time ms", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg8 = {"max delay time ms", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg9 = {"ADS communication timeout ms", iocshArgInt};
  static const iocshArg adsAsynPortDriverConfigureArg10 = {"default time source (EPCIS=0,PLC=1)", iocshArgInt};
  static const iocshArg *adsAsynPortDriverConfigureArgs[] = {
      &adsAsynPortDriverConfigureArg0, &adsAsynPortDriverConfigureArg1,
      &adsAsynPortDriverConfigureArg2, &adsAsynPortDriverConfigureArg3,
      &adsAsynPortDriverConfigureArg4, &adsAsynPortDriverConfigureArg5,
      &adsAsynPortDriverConfigureArg6, &adsAsynPortDriverConfigureArg7,
      &adsAsynPortDriverConfigureArg8, &adsAsynPortDriverConfigureArg9,
      &adsAsynPortDriverConfigureArg10};

  static const iocshFuncDef adsAsynPortDriverConfigureFuncDef =
      {"adsAsynPortDriverConfigure", 11, adsAsynPortDriverConfigureArgs};

  static void adsAsynPortDriverConfigureCallFunc(const iocshArgBuf *args)
  {
    adsAsynPortDriverConfigure(args[0].sval, args[1].sval, args[2].sval, args[3].ival, args[4].ival, args[5].ival, args[6].ival, args[7].ival, args[8].ival, args[9].ival, args[10].ival);
  }

  /*
   * adsSetLocalAddress("ams_net_id")
   */
  static const iocshArg adsSetLocalAddressArg0 = {"local_ams_id", iocshArgString};
  static const iocshArg *adsSetLocalAddressArgs[] = {&adsSetLocalAddressArg0};
  static const iocshFuncDef adsSetLocalAddressFuncDef = {"adsSetLocalAddress", 1, adsSetLocalAddressArgs};

  static void adsSetLocalAddressCallFunc(const iocshArgBuf *args)
  {
    if (!args[0].sval || strlen(args[0].sval) < 11)
    {
      printf("%s:%s: local_ams_id parameter required (of the form A.B.C.D.E.F)\n", driverName, __func__);
      return;
    }
    printf("%s:%s: Setting local AMS Net ID to: %s\n", driverName, __func__, args[0].sval);
    AdsSetLocalAddress(std::string(args[0].sval));
  }

  /*
   * adsPollInfo("name")
   */
  static const iocshArg adsPollInfoArg0 = {"name", iocshArgString};
  static const iocshArg *adsPollInfoArgs[] = {&adsPollInfoArg0};
  static const iocshFuncDef adsPollInfoFuncDef = {"adsPollInfo", 1, adsPollInfoArgs};

  static void adsPollInfoCallFunc(const iocshArgBuf *args)
  {
    adsAsynPortObj->poll_info(args[0].sval);
  }

  /*
   * This routine is called before multitasking has started, so there's
   * no race condition in the test/set of firstTime.
   */

  static void adsAsynPortDriverRegister(void)
  {
    iocshRegister(&adsAsynPortDriverConfigureFuncDef, adsAsynPortDriverConfigureCallFunc);
    iocshRegister(&adsSetLocalAddressFuncDef, adsSetLocalAddressCallFunc);
    iocshRegister(&adsPollInfoFuncDef, adsPollInfoCallFunc);
  }

  epicsExportRegistrar(adsAsynPortDriverRegister);
}

long adsAsynPortDriver::getAdsClientPortNumberForThreadId(epicsThreadId threadId)
{
  std::lock_guard<std::recursive_mutex> lockGuard(threadIdToAmsClientPortMapMutex_);

  // If the map doesn't contain the thread id already, return the default one.
  if (threadIdToAmsClientPortMap_.find(threadId) != threadIdToAmsClientPortMap_.end())
    return threadIdToAmsClientPortMap_.at(0).port;

  // Otherwise, get the ads client port assigned to the input threadId.
  return threadIdToAmsClientPortMap_.at(threadId).port;
}
long adsAsynPortDriver::addAdsClientPortNumberForThreadId(epicsThreadId threadId)
{
  std::lock_guard<std::recursive_mutex> lockGuard(threadIdToAmsClientPortMapMutex_);

  // If the map contains the thread id already, increment the live count
  // and return the port number used for it.
  auto it = threadIdToAmsClientPortMap_.find(threadId);
  if (it != threadIdToAmsClientPortMap_.end())
  {
    it->second.liveCount++;
    return it->second.port;
  }

  // If we get here, the thread id doesn't already have an ads client port assigned for it.
  // In this case, open a new port for it.
  auto adsClientPort = AdsPortOpenEx();
  if (isInvalidPortNumber(adsClientPort))
    return 0;

  // Insert the new pair.
  threadIdToAmsClientPortMap_.insert(std::make_pair(threadId, AmsClientPortEntry(adsClientPort, 1)));
  return adsClientPort;
}
asynStatus adsAsynPortDriver::delAdsClientPortNumberForThreadId(epicsThreadId threadId)
{
  std::lock_guard<std::recursive_mutex> lockGuard(threadIdToAmsClientPortMapMutex_);

  // If the map contains the thread id erase it.
  auto it = threadIdToAmsClientPortMap_.find(threadId);
  if (it != threadIdToAmsClientPortMap_.end())
  {
    it->second.liveCount--;
    if (it->second.liveCount > 0)
    {
      // Do not close the port if the live count is still above 0.
      // This means this thread is still using this port somewhere.
      return asynSuccess;
    }
    auto result = AdsPortCloseEx(it->second.port);
    if (result)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
                "%s:%s: found thread id in map but failed to close port.\n", driverName, __func__);
      return asynError;
    }
    threadIdToAmsClientPortMap_.erase(it);
  }
  return asynSuccess;
}

bool adsAsynPortDriver::okToProcessBulkReads()
{
  std::lock_guard<std::recursive_mutex> lg(adsBulkInfoUpdateMutex_);
  return okToProcessBulkReads_;
}
bool adsAsynPortDriver::setOkToProcessBulkReads(bool ok)
{
  std::lock_guard<std::recursive_mutex> lg(adsBulkInfoUpdateMutex_);
  okToProcessBulkReads_ = ok;
  return okToProcessBulkReads_;
}
adsAsynPortDriver::datacbinfo::datacbinfo(adsParamInfo &paramInfo, const AdsNotificationHeader *pNotification) : paramInfo(paramInfo), notification(*pNotification)
{
  const uint8_t *dataFromNotification = reinterpret_cast<const uint8_t *>(pNotification + 1);
  data.resize(pNotification->cbSampleSize);
  memcpy(data.data(), dataFromNotification, pNotification->cbSampleSize);
}
void adsAsynPortDriver::emplaceInDataCallbackQueue(adsParamInfo &paramInfo, const AdsNotificationHeader *pNotification)
{
  std::lock(dataCbQueueMutex_, *paramInfo.mutex);
  std::lock_guard<std::mutex> lockGuard(dataCbQueueMutex_, std::adopt_lock);
  std::lock_guard<std::recursive_mutex> lg(*paramInfo.mutex, std::adopt_lock);
  if (!pNotification)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: notification pointer null, skip %s (%d)\n",
              driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
    return;
  }
  if (datacbqueue.size() >= MAXCBQSIZE)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: datacbqueue at max size, skip %s (%d)\n",
              driverName, __func__, paramInfo.drvInfo.c_str(), paramInfo.paramIndex);
    return;
  }
  datacbqueue.emplace(paramInfo, pNotification);
}

AdsClientPortGuard::AdsClientPortGuard(adsAsynPortDriver &adsAsynPortDriver, long &adsClientPort)
    : adsAsynPortDriver_(adsAsynPortDriver)
{
  threadId_ = epicsThreadGetIdSelf();
  adsClientPort_ = adsAsynPortDriver_.addAdsClientPortNumberForThreadId(threadId_);
  adsClientPort = adsClientPort_;
}
AdsClientPortGuard::~AdsClientPortGuard()
{
  adsAsynPortDriver_.delAdsClientPortNumberForThreadId(threadId_);
}
long AdsClientPortGuard::getAdsClientPort() const
{
  return adsClientPort_;
}