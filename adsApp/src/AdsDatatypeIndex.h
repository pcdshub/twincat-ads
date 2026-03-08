#ifndef ADSDATATYPEINDEX_H_
#define ADSDATATYPEINDEX_H_

#include "AdsDatatypeEntry.h"
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

struct AdsDatatypeEntryExpanded : public AdsDatatypeEntry
{
  std::string name;
  std::string type;
  std::string comment;
  std::string flagStr;
  std::list<std::shared_ptr<AdsDatatypeEntryExpanded>> children;
  uint32_t indexOffsetFromRoot = 0;
  uint32_t numArrayElements = 0;

  AdsDatatypeEntryExpanded(const AdsDatatypeEntry &adsDatatypeEntry,
                           const std::unordered_map<std::string, const AdsDatatypeEntry *> &datatypeEntryRawIndex,
                           uint32_t startingOffset = 0, const std::string &prefix = "");
};

class AdsDatatypeIndex
{
public:
  AdsDatatypeIndex(const std::vector<char> &datatypeUpload);

  std::shared_ptr<AdsDatatypeEntryExpanded> lookup(const std::string &name)
  {
    auto it = mDatatypeEntryIndex.find(name);
    if (it == mDatatypeEntryIndex.end())
      return nullptr;
    return mDatatypeEntryIndex.at(name);
  }

  static uint32_t getNumberOfElementsInArray(const AdsDatatypeEntry &adsDatatypeEntry,
                                             const std::unordered_map<std::string, const AdsDatatypeEntry *> &datatypeEntryRawIndex)
  {
    uint32_t numArrayElements = 0;
    if (adsDatatypeEntry.arrayDim != 0)
    {
      numArrayElements = 1;
      for (int arrayDim = 0; arrayDim < adsDatatypeEntry.arrayDim; ++arrayDim)
      {
        auto arrayInfo = adsDatatypeEntry.arrayInfo()[arrayDim];
        numArrayElements *= arrayInfo.elements;
      }
      std::string typeStr = adsDatatypeEntry.type();
      auto it = datatypeEntryRawIndex.find(typeStr);
      if (it != datatypeEntryRawIndex.end())
      {
        auto type = datatypeEntryRawIndex.at(typeStr);
        if (numArrayElements * type->size != adsDatatypeEntry.size)
        {
          std::cerr << "Something went wrong for datatype: [" << typeStr << "] in [" << adsDatatypeEntry.name() << "]\n"
                    << "The number of array elements: [" << numArrayElements << "] "
                    << "multiplied by the size of the datatype: [" << type->size << "] "
                    << "does not equal the size of the datatype entry: [" << adsDatatypeEntry.size << "]\n"
                    << std::endl;
          return 0;
        }
      }
      else
      {
        std::cerr << "Could not find type: [" << typeStr << "] in [" << adsDatatypeEntry.name() << "] within the raw index of datatypes.\n"
                  << std::endl;
        return 0;
      }
    }
    return numArrayElements;
  }

  static std::list<std::string> expandArrayIndices(const AdsDatatypeEntry &adsDatatypeRawEntry)
  {
    std::list<std::string> arrayIndices;
    arrayIndices.push_back("");
    for (int arrayDim = adsDatatypeRawEntry.arrayDim - 1; arrayDim >= 0; --arrayDim)
    {
      auto arrayInfo = adsDatatypeRawEntry.arrayInfo()[arrayDim];
      std::list<std::string> newArrayIndices;
      for (uint i = arrayInfo.lBound; i < arrayInfo.lBound + arrayInfo.elements; ++i)
      {
        for (const auto &name : arrayIndices)
        {
          std::stringstream ss;
          if (name.empty())
            ss << i;
          else
            ss << i << "," << name;
          newArrayIndices.push_back(ss.str());
        }
      }
      arrayIndices = newArrayIndices;
    }
    arrayIndices.remove_if([](const std::string &index)
                           { return index.empty(); });
    return arrayIndices;
  }

  const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &getDatatypeEntryIndex() const;

private:
  const std::vector<char> &mDatatypeUpload;
  std::unordered_map<std::string, const AdsDatatypeEntry *> mDatatypeEntryRawIndex;
  std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> mDatatypeEntryIndex;
};

#endif // ADSDATATYPEINDEX_H_