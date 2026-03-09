#ifndef ADSSYMBOLINDEX_H_
#define ADSSYMBOLINDEX_H_

#include "AdsDatatypeIndex.h"
#include "AdsDef.h"

#include <iostream>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

std::string adsSymbolFlagsToString(uint32_t flags);

#pragma pack(push, 1)

struct AdsSymbolEntryAccess : public AdsSymbolEntry
{
  const char *name() const { return reinterpret_cast<const char *>(this + 1); }
  const char *type() const { return name() + nameLength + 1; }
  const char *comment() const { return type() + typeLength + 1; }
  const AdsSymbolEntryAccess *maybeNext() const
  {
    return reinterpret_cast<const AdsSymbolEntryAccess *>(reinterpret_cast<const char *>(this) + entryLength);
  }
};

const char *name(const AdsSymbolEntry *adsSymbolEntry);
const char *type(const AdsSymbolEntry *adsSymbolEntry);
const char *comment(const AdsSymbolEntry *adsSymbolEntry);

struct AdsSymbolEntryExpanded : public AdsSymbolEntry
{
  std::string name;
  std::string type;
  std::string comment;
  std::string flagStr;
  std::list<std::shared_ptr<AdsSymbolEntryExpanded>> children;

  AdsSymbolEntryExpanded() = default;
  AdsSymbolEntryExpanded(const AdsSymbolEntryAccess &adsSymbolEntryRoot,
                         const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &datatypeEntryIndex,
                         const std::string &prefix);
  AdsSymbolEntryExpanded(const AdsSymbolEntryExpanded &adsSymbolEntryExpandedParent,
                         const AdsDatatypeEntryExpanded::Child &adsDatatypeEntryChild);
};

#pragma pack(pop)

class AdsSymbolIndex
{
public:
  AdsSymbolIndex(const std::vector<char> &symbolUpload,
                 const AdsDatatypeIndex &adsDatatypeIndex);

  const std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &getSymbolEntryIndex() const;
  void writeTree(std::ostream &buffer,
                 std::shared_ptr<AdsSymbolEntryExpanded> startingNode,
                 size_t numLevels = 1,
                 size_t numTabs = 0);

private:
  const std::vector<char> &mSymbolUpload;
  std::unordered_map<std::string, const AdsSymbolEntryAccess *> mSymbolEntryRawIndex;
  const AdsDatatypeIndex &mAdsDatatypeIndex;
  std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> mSymbolEntryIndex;
};

#endif // ADSSYMBOLINDEX_H_
