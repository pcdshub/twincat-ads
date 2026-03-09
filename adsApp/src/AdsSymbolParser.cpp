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
                           const AmsAddr &amsAddr)
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

  std::cout << "Read " << mDatatypes.size() << " Bytes of datatype information. Creating datatype index..." << std::endl;
  mDatatypeIndex = std::make_shared<AdsDatatypeIndex>(mDatatypes);
  std::cout << "Datatype index created with " << mDatatypeIndex->getDatatypeEntryIndex().size() << " unique types." << std::endl;
  std::cout << "Read " << mSymbols.size() << " Bytes of symbol information. Creating symbol index..." << std::endl;
  mSymbolIndex = std::make_shared<AdsSymbolIndex>(mSymbols, *mDatatypeIndex);
  std::cout << "Symbol index created with " << mSymbolIndex->getSymbolEntryIndex().size() << " root symbol nodes." << std::endl;
  std::cout << "Beginning to fill the symbol map..." << std::endl;
  for (auto pair : mSymbolIndex->getSymbolEntryIndex())
  {
    // mSymbolIndex->writeTree(std::cout, pair.second, 2);
    fillAdsSymbolMapFromStartingNode(pair.second, mAdsSymbolMap);
  }
  std::cout << "Symbol map filled. Total size reached: " << mAdsSymbolMap.size() << std::endl;
  return errorCode;
}

void AdsSymbolParser::fillAdsSymbolMapFromStartingNode(const std::shared_ptr<AdsSymbolEntryExpanded> startingNode,
                                                       std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &adsSymbolMap) const
{
  if (!startingNode)
    return;

  adsSymbolMap.insert(std::make_pair(startingNode->name, startingNode));

  for (auto child : startingNode->children)
  {
    fillAdsSymbolMapFromStartingNode(child, adsSymbolMap);
  }
}

const std::shared_ptr<AdsSymbolEntryExpanded> AdsSymbolParser::lookup(const std::string &variableName) const
{
  auto it = mAdsSymbolMap.find(variableName);
  if (it == mAdsSymbolMap.end())
  {
    return nullptr;
  }
  return it->second;
}