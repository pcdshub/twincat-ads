
#ifndef ADSASYNPORTDRIVER_H_
#define ADSASYNPORTDRIVER_H_

#include "asynPortDriver.h"
#include <epicsEvent.h>
#include <dbCommon.h>
#include <dbBase.h>
#include <dbStaticLib.h>

#define MAX_FIELD_CHAR_LENGTH 128


typedef struct adsParamInfo{
  char *recordName;
  char *recordType;
  char *scan;
  char *dtyp;
  char *inp;
  char *out;
  char *drvInfo;
  asynParamType asynType;
  bool isInput;
  bool isOutput;
  int adsPort;
  int paramIndex;
}adsParamInfo;

class adsAsynPortDriver : public asynPortDriver {
public:
  adsAsynPortDriver(const char *portName,
                      const char *ipaddr,
                      const char *amsaddr,
                      unsigned int amsport,
                      int paramTableSize,
                      unsigned int priority,
                      int autoConnect,
                      int noProcessEos);
  virtual ~adsAsynPortDriver();
  virtual void report(FILE *fp, int details);
  virtual asynStatus disconnect(asynUser *pasynUser);
  virtual asynStatus connect(asynUser *pasynUser);
  virtual asynStatus drvUserCreate(asynUser *pasynUser,const char *drvInfo,const char **pptypeName,size_t *psize);
  virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual);
  virtual asynStatus readOctet(asynUser *pasynUser, char *value, size_t maxChars,size_t *nActual, int *eomReason);
  virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
  virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
  virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
  virtual asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value,size_t nElements, size_t *nIn);
  virtual asynStatus readInt16Array(asynUser *pasynUser, epicsInt16 *value,size_t nElements, size_t *nIn);
  virtual asynStatus readInt32Array(asynUser *pasynUser, epicsInt32 *value,size_t nElements, size_t *nIn);
  virtual asynStatus readFloat32Array(asynUser *pasynUser, epicsFloat32 *value,size_t nElements, size_t *nIn);
  virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,size_t nElements, size_t *nIn);
  asynStatus setCfgData(const char *portName,
                          const char *ipaddr,
                          const char *amsaddr,
                          unsigned int amsport,
                          unsigned int priority,
                          int noAutoConnect,
                          int noProcessEos);
  asynUser *getTraceAsynUser();
protected:

private:
  asynStatus getRecordInfoFromDrvInfo(const char *drvInfo,adsParamInfo *paramInfo);
  asynStatus getParamDataType(const char *drvInfo,asynParamType *type);
  asynParamType dtypStringToAsynType(char *dtype);
  void printParamInfo(adsParamInfo *paramInfo);
  void dbDumpRecords();
  asynStatus connectIt( asynUser *pasynUser);
  epicsEventId eventId_;
  const char *portName_;
  const char *ipaddr_;
  const char *amsaddr_;
  unsigned int amsport_;
  unsigned int priority_;
  int noAutoConnect_;
  int noProcessEos_;
  adsParamInfo **pAdsParamArray_;
  int pAdsParamArrayCount_;
  int paramTableSize_;
};

#endif /* ADSASYNPORTDRIVER_H_ */



