#ifndef ADSDATATYPEINDEX_H_
#define ADSDATATYPEINDEX_H_

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

struct AdsDatatypeEntry;

class AdsDatatypeIndex
{
public: // types
  class Entry;

public: // methods
  AdsDatatypeIndex(const std::vector<char> &dataTypeUpload)
      : mDataTypeUpload(dataTypeUpload)
  {
    build();
  }
  AdsDatatypeIndex(AdsDatatypeIndex &&) = default;
  ~AdsDatatypeIndex();

  const Entry *lookup(const std::string &name) const
  {
    return mNameIndex.at(name);
  }

  const std::list<const Entry *> &entries() const { return mEntries; }

private: // methods
  void build();

private: // attributes
  std::vector<char> mDataTypeUpload;
  std::unordered_map<std::string, const AdsDatatypeEntry *> mNameRawIndex;
  std::list<const Entry *> mEntries;
  std::unordered_map<std::string, const Entry *> mNameIndex;
};

class AdsDatatypeIndex::Entry
{
public: // methods
  Entry(const std::string &name, uint32_t offset, const AdsDatatypeEntry *adsType, const Entry *parent = nullptr);
  ~Entry();

  const Entry *parent() const { return mParent; }
  const AdsDatatypeEntry *adsType() const { return mAdsType; }
  int childCount(const AdsDatatypeIndex &index) const;
  std::list<const Entry *> children(const AdsDatatypeIndex &index);

  std::string name() const { return mName; }
  std::string fullName() const;
  uint32_t offset() const;

private:
  static int arrayCount(const AdsDatatypeEntry *adsType, const AdsDatatypeIndex &index);

  const Entry *mParent = nullptr;
  std::string mName;
  uint32_t mOffset = 0;
  const AdsDatatypeEntry *mAdsType = nullptr;
  bool mChildrenLoaded = false;
  std::list<const Entry *> mChildren;
};

#endif // ADSDATATYPEINDEX_H_