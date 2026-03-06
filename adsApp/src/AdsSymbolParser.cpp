#include "AdsSymbolParser.h"
#include "AdsDatatypeEntry.h"
#include "AdsDatatypeIndex.h"
#include "AdsSymbolIndex.h"
#include "AdsSymbolUploadInfo2.h"

long AdsSymbolParser::retrieveSymbolsAndTypes()
{
  mSymbols.clear();
  mDatatypes.clear();

  try
  {
    auto symbolUploadInfo = AdsSymbolUploadInfo2::fromDevice(*mAdsDevice);
    mSymbols = symbolUploadInfo.uploadSymbols(*mAdsDevice);
    mDatatypes = symbolUploadInfo.uploadDatatypes(*mAdsDevice);
  }
  catch (const std::exception &e)
  {
    return 1;
  }
}