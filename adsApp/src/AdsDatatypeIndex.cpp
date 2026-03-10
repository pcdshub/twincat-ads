#include "AdsDatatypeIndex.h"
#include "AdsDatatypeEntry.h"
#include <assert.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <stack>

AdsDatatypeIndex::AdsDatatypeIndex(const std::vector<char> &datatypeUpload)
    : mDatatypeUpload(datatypeUpload)
{
  auto end = mDatatypeUpload.data() + mDatatypeUpload.size();
  auto current = reinterpret_cast<const AdsDatatypeEntry *>(mDatatypeUpload.data());

  while (reinterpret_cast<const char *>(current) < end)
  {
    auto next = reinterpret_cast<const AdsDatatypeEntry *>(reinterpret_cast<const char *>(current) + current->entryLength);
    if (reinterpret_cast<const char *>(next) > end)
    {
      std::cerr << "The datatype record retrieved extends past the end of the buffer. Some datatypes may be lost." << std::endl;
      break;
    }
    std::string currentName = current->name();
    mDatatypeEntryRawIndex[currentName] = current;
    current = next;
  }
  for (auto &pair : mDatatypeEntryRawIndex)
  {
    // Pre-allocate a pointer to each unique expanded datatype entry.
    // These pointers will serve as placeholders so we don't have to recursively
    // traverse through each datatype.
    mDatatypeEntryIndex.insert(std::make_pair(
        pair.first,
        std::make_shared<AdsDatatypeEntryExpanded>(*pair.second)));
  }
  for (auto &pair : mDatatypeEntryIndex)
  {
    // For each pre-allocated entry, now expand it.
    pair.second->expand(mDatatypeEntryRawIndex, mDatatypeEntryIndex);
  }
}

const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &AdsDatatypeIndex::getDatatypeEntryIndex() const
{
  return mDatatypeEntryIndex;
}

AdsDatatypeEntryExpanded::AdsDatatypeEntryExpanded(const AdsDatatypeEntry &adsDatatypeEntry) : rawDatatypeEntry(adsDatatypeEntry) {}

void AdsDatatypeEntryExpanded::expand(const std::unordered_map<std::string, const AdsDatatypeEntry *> &datatypeEntryRawIndex,
                                      const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &datatypeEntryIndex)
{
  flagStr = adsDatatypeFlagsToString(rawDatatypeEntry.flags);
  this->typeName = rawDatatypeEntry.name();
  this->comment = rawDatatypeEntry.comment();

  // Let's handle the case of this datatype having structured data members.
  // Grab the pointer to the first item in the sub items buffer.
  // It might be null, but the sub item count should be 0 in that case so we won't
  // use it.
  auto subItem = rawDatatypeEntry.subItems();
  for (int subItemIndex = 0; subItemIndex < rawDatatypeEntry.subItemCount; ++subItemIndex)
  {
    if (!subItem)
    {
      std::cout << "Type: ["
                << this->typeName
                << "] has a malformed subitem."
                << std::endl;
      break;
    }
    // We only know the name of the sub item if we grab it now.
    // So we pass it down to the expanded child structure below so
    // it knows what its name is. Otherwise, it would be like a root
    // datatype and not know its name until instantiation.
    std::string subItemName = subItem->name();
    std::string subItemType = subItem->type();

    auto datatypeEntryIndexIt = datatypeEntryIndex.find(subItemType);
    if (datatypeEntryIndexIt != datatypeEntryIndex.end())
    {
      // If we have already expanded an entry of this type we can just copy the
      // pointer to it. No need to redo the work of expanding it.
      auto child = std::make_shared<Child>(datatypeEntryIndexIt->second,
                                           subItemName,
                                           subItem->offs);
      this->children.push_back(child);
    }
    else
    {
      std::cout << "Could not find type: ["
                << subItemType
                << "] in ["
                << this->typeName
                << "] within the index of datatypes."
                << std::endl;
    }
    subItem = reinterpret_cast<const AdsDatatypeEntry *>(reinterpret_cast<const char *>(subItem) + subItem->entryLength);
  }

  // Let's handle the case of this datatype being an array.
  auto numArrayElements = AdsDatatypeIndex::getNumberOfElementsInArray(rawDatatypeEntry, datatypeEntryRawIndex);
  auto arrayIndices = AdsDatatypeIndex::expandArrayIndices(rawDatatypeEntry);
  if (numArrayElements != arrayIndices.size())
  {
    std::cout << "Number of array indices and number of elements did not match for ["
              << rawDatatypeEntry.name()
              << "] of type ["
              << rawDatatypeEntry.type() << "]."
              << std::endl;
    return;
  }
  if (!arrayIndices.empty())
  {
    this->isArray = true;
    if (rawDatatypeEntry.size % arrayIndices.size() != 0)
    {
      std::cout << "Size of ["
                << rawDatatypeEntry.name()
                << "] is not divisible by the number of array indices: ["
                << arrayIndices.size()
                << "]"
                << std::endl;
      return;
    }
    else
    {
      // We know the current entry we are building is an array type, so its name is something like:
      // ARRAY [0..99] OF INT <-- this->rawDatatypeEntry.name(), for example.
      // The type for this would be INT <-- this->rawDatatypeEntry.type().
      // So in this case, we would need to look up INT in the raw index to get its entry.
      std::string arrayElementType = this->rawDatatypeEntry.type();
      auto datatypeEntryIndexIt = datatypeEntryIndex.find(arrayElementType);
      if (datatypeEntryIndexIt != datatypeEntryIndex.end())
      {
        uint32_t offset = 0;
        auto elementSize = datatypeEntryIndexIt->second->rawDatatypeEntry.size;
        for (const auto &arrayIndex : arrayIndices)
        {
          // We count the type at each array index of this entry as a child.
          // We name is the index in square brackets because this is how the
          // name will be represented over ADS.
          auto child = std::make_shared<Child>(datatypeEntryIndexIt->second,
                                               "[" + arrayIndex + "]",
                                               offset);
          this->children.push_back(child);
          offset += elementSize;
        }
      }
      else
      {
        std::cout << "Could not find type: ["
                  << arrayElementType
                  << "] in ["
                  << this->typeName
                  << "] within the index of datatypes."
                  << std::endl;
      }
    }
  }
}