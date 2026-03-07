#ifndef ADSSYMBOLPARSER_H_
#define ADSSYMBOLPARSER_H_

#include "AdsDatatypeIndex.h"
#include "AdsSymbolIndex.h"
#include "AdsSymbolUploadInfo2.h"
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

struct SymbolNode
{
  std::string symbolName;
  std::string symbolType;
  SymbolNode *parent = nullptr;
  std::list<SymbolNode *> children;
  const AdsSymbolEntry *symbol = nullptr;
  const AdsDatatypeIndex::Entry *datatypeEntry = nullptr;
  uint32_t group() const
  {
    return symbol ? symbol->iGroup : 0;
  }
  uint32_t offset() const
  {
    return (symbol ? symbol->iOffs : 0) + (datatypeEntry ? datatypeEntry->offset() : 0);
  }
};

class AdsSymbolParser
{
public:
  AdsSymbolParser(long adsClientPort, const AmsAddr &amsAddr);
  ~AdsSymbolParser();
  
  long load(std::unordered_map<std::string, const AdsSymbolEntry *> &adsSymbolMap);
  void printTree(SymbolNode *node);

private:
  void buildSymbolTree();
  void fillAdsSymbolMap(SymbolNode *node, std::unordered_map<std::string, const AdsSymbolEntry *> &adsSymbolMap);
  void addSymbol(SymbolNode *parentNode, const AdsSymbolEntry *symbol, const std::string& symbolName, const std::string& symbolType, AdsDatatypeIndex::Entry *type = nullptr);

  long mAdsClientPort;
  AmsAddr mAmsAddr;
  std::vector<char> mSymbols;
  std::vector<char> mDatatypes;
  std::shared_ptr<AdsDatatypeIndex> mDatatypeIndex;
  std::shared_ptr<AdsSymbolIndex> mSymbolIndex;
  SymbolNode *mRootNode;
};

#endif // ADSSYMBOLPARSER_H_