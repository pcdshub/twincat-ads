#include "AdsSymbolParser.h"
#include "AdsDatatypeEntry.h"
#include "AdsDatatypeIndex.h"
#include "AdsSymbolIndex.h"
#include "AdsSymbolUploadInfo2.h"
#include <iostream>

AmsAddr AdsSymbolParser::getAmsAddressLoaded() const
{
  return mAmsAddr;
}

long AdsSymbolParser::load(long adsClientPort,
                           const AmsAddr &amsAddr,
                           std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &adsSymbolMap)
{
  mAdsClientPort = adsClientPort;
  mAmsAddr = amsAddr;
  mSymbols.clear();
  mDatatypes.clear();

  long errorCode = 0;
  auto symbolUploadInfo = AdsSymbolUploadInfo2::fromAmsAddr(adsClientPort, amsAddr, errorCode);
  if (errorCode)
  {
    printf("Failed to read symbol upload info. Error code: %lu\n", errorCode);
    return errorCode;
  }
  errorCode = symbolUploadInfo.uploadSymbols(adsClientPort, amsAddr, mSymbols);
  if (errorCode)
  {
    printf("Failed to read symbols. Error code: %lu\n", errorCode);
    return errorCode;
  }
  errorCode = symbolUploadInfo.uploadDatatypes(adsClientPort, amsAddr, mDatatypes);
  if (errorCode)
  {
    printf("Failed to read symbol datatypes. Error code: %lu\n", errorCode);
    return errorCode;
  }

  mDatatypeIndex = std::make_shared<AdsDatatypeIndex>(mDatatypes);
  mSymbolIndex = std::make_shared<AdsSymbolIndex>(mSymbols, *mDatatypeIndex);

  std::cout << "Number of root symbol nodes: " << mSymbolIndex->getSymbolEntryIndex().size() << std::endl;
  std::cout << "Begin filling symbol map..." << std::endl;
  for (auto pair : mSymbolIndex->getSymbolEntryIndex())
  {
    fillAdsSymbolMapFromStartingNode(pair.second, adsSymbolMap);
  }
  std::cout << "Symbol map filled. Total size reached: " << adsSymbolMap.size() << std::endl;

  return errorCode;
}

void AdsSymbolParser::fillAdsSymbolMapFromStartingNode(const std::shared_ptr<AdsSymbolEntryExpanded> startingNode,
                                      std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &adsSymbolMap) const
{
  if (!startingNode)
    return;

  if (adsSymbolMap.size() % 1000 == 0)
  {
    std::cout << "Map size at: " << adsSymbolMap.size() << std::endl;
  }

  adsSymbolMap.insert(std::make_pair(startingNode->name, startingNode));

  for (auto child : startingNode->children)
  {
    fillAdsSymbolMapFromStartingNode(child, adsSymbolMap);
  }
}