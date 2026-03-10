#ifndef ADSSYMBOLPARSER_H_
#define ADSSYMBOLPARSER_H_

#include "AdsDatatypeIndex.h"
#include "AdsSymbolIndex.h"
#include "AdsSymbolUploadInfo2.h"
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AdsSymbolParser
{
public:
  long load(long adsClientPort,
            const AmsAddr &amsAddr);

  const std::shared_ptr<AdsSymbolEntryExpanded> lookup(const std::string& variableName) const;
  AmsAddr getAmsAddressLoaded() const;

private:
  void fillAdsSymbolMapFromStartingNode(const std::shared_ptr<AdsSymbolEntryExpanded> startingNode,
                                        std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &adsSymbolMap) const;

  long mAdsClientPort = 0;
  AmsAddr mAmsAddr;
  std::vector<char> mSymbols;
  std::vector<char> mDatatypes;
  std::shared_ptr<AdsDatatypeIndex> mDatatypeIndex;
  std::shared_ptr<AdsSymbolIndex> mSymbolIndex;
  std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> mAdsSymbolMap;
};

#endif // ADSSYMBOLPARSER_H_