#ifndef ADSSYMBOLINDEX_H_
#define ADSSYMBOLINDEX_H_

#include "AdsDef.h"

#include <list>
#include <unordered_map>
#include <vector>

struct AdsSymbolEntryAccess : public AdsSymbolEntry
{
  static std::string adsSymbolFlagsToString(uint32_t flags);

public:
  const char *name() const { return reinterpret_cast<const char *>(this + 1); }
  const char *type() const { return name() + nameLength + 1; }
  const char *comment() const { return type() + typeLength + 1; }
  const AdsSymbolEntryAccess *maybeNext() const
  {
    return reinterpret_cast<const AdsSymbolEntryAccess *>(reinterpret_cast<const char *>(this) + entryLength);
  }
};

class AdsSymbolIndex
{
public: // methods
  AdsSymbolIndex(const std::vector<char> &symbolUpload)
      : mSymbolUpload(symbolUpload)
  {
    build();
  }
  AdsSymbolIndex(AdsSymbolIndex &&) = default;

  const std::list<const AdsSymbolEntryAccess *> &entries() const { return mEntries; }

private: // methods
  void build();

private: // attributes
  const std::vector<char>& mSymbolUpload;
  std::list<const AdsSymbolEntryAccess *> mEntries;
  std::unordered_map<std::string, const AdsSymbolEntryAccess *> mNameIndex;
};

#endif // ADSSYMBOLINDEX_H_
