#ifndef ADSSYMBOLUPLOADINFO2_H_
#define ADSSYMBOLUPLOADINFO2_H_

#include "AdsDef.h"
#include <vector>

struct AdsSymbolUploadInfo2
{
  uint32_t nSymbols = 0;
  uint32_t nSymSize = 0;
  uint32_t nDatatypes = 0;
  uint32_t nDatatypeSize = 0;
  uint32_t nMaxDynSymbols = 0;
  uint32_t nUsedDynSymbols = 0;

  static AdsSymbolUploadInfo2 fromAmsAddr(long adsClientPort, const AmsAddr &amsAddr);

  long uploadSymbols(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &symbols) const;
  long uploadDatatypes(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &datatypes) const;
};

#endif // ADSSYMBOLUPLOADINFO2_H_