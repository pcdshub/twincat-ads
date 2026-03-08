#include "AdsDatatypeIndex.h"
#include "AdsDatatypeEntry.h"
#include "adsAsynPortDriverUtils.h"
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
    std::string currentType = current->type();
    std::cout << "Found unique datatype: Name: " << currentName << " Type: " << currentType << std::endl;
    mDatatypeEntryRawIndex[currentName] = current;
    current = next;
  }
  for (auto &pair : mDatatypeEntryRawIndex)
  {
    std::cout << "Expanding datatype: Name: " << pair.first << " Type: " << pair.second->type() << std::endl;
    // The root datatype item has no name. This is because the name of the root datatype is only
    // assigned at the instantiation of a symbol.
    // So the root datatype name is blank and once it gets matched up to a symbol then
    // it will take the name of the symbol.
    mDatatypeEntryIndex.insert(std::make_pair(
        pair.first,
        std::make_shared<AdsDatatypeEntryExpanded>(*pair.second,
                                                   mDatatypeEntryRawIndex,
                                                   0, "")));
  }
}

const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &AdsDatatypeIndex::getDatatypeEntryIndex() const
{
  return mDatatypeEntryIndex;
}

AdsDatatypeEntryExpanded::AdsDatatypeEntryExpanded(const AdsDatatypeEntry &adsDatatypeEntry,
                                                   const std::unordered_map<std::string, const AdsDatatypeEntry *> &datatypeEntryRawIndex,
                                                   uint32_t startingOffset, const std::string &itemName) : AdsDatatypeEntry(adsDatatypeEntry)
{
  flagStr = adsDatatypeFlagsToString(flags);
  this->name = itemName;
  this->type = adsDatatypeEntry.type();
  this->comment = adsDatatypeEntry.comment();

  this->indexOffsetFromRoot = startingOffset + adsDatatypeEntry.offs;

  // Let's handle the case of this datatype having structured data members.
  // Grab the pointer to the first item in the sub items buffer.
  // It might be null, but the sub item count should be 0 in that case so we won't
  // use it.
  auto subItem = adsDatatypeEntry.subItems();
  for (int subItemIndex = 0; subItemIndex < adsDatatypeEntry.subItemCount; ++subItemIndex)
  {
    if (!subItem)
    {
      std::cerr << string_format("Name: [%s] Type: [%s] has a malformed subitem.\n",
                                 name, type)
                << std::endl;
      break;
    }
    // We only know the name of the sub item if we grab it now.
    // So we pass it down to the expanded child structure below so
    // it knows what its name is. Otherwise, it would be like a root
    // datatype and not know its name until instantiation.
    std::string subItemName = subItem->name();
    std::string subItemType = subItem->type();
    auto it = datatypeEntryRawIndex.find(subItemType);
    if (it != datatypeEntryRawIndex.end())
    {
      auto type = datatypeEntryRawIndex.at(subItemType);
      auto child = std::make_shared<AdsDatatypeEntryExpanded>(*type,
                                                              datatypeEntryRawIndex,
                                                              this->indexOffsetFromRoot + subItem->offs,
                                                              "." + subItemName);
      this->children.push_back(child);
      std::cout << "Found sub item: Name: " << child->name << " Type: " << child->type << " Offset: " << child->indexOffsetFromRoot << std::endl;
    }
    else
    {
      std::cerr << string_format("Could not find type: [%s] in [%s] within the raw index of datatypes.\n",
                                 subItemType, subItemName)
                << std::endl;
    }
    subItem = reinterpret_cast<const AdsDatatypeEntry *>(reinterpret_cast<const char *>(subItem) + subItem->entryLength);
  }

  // Let's handle the case of this datatype being an array.
  this->numArrayElements = AdsDatatypeIndex::getNumberOfElementsInArray(adsDatatypeEntry, datatypeEntryRawIndex);
  auto arrayIndices = AdsDatatypeIndex::expandArrayIndices(adsDatatypeEntry);
  if (this->numArrayElements != arrayIndices.size())
  {
    std::cerr << string_format("Number of array indices and number of elements did not match for %s of type %s.",
                               this->name, this->type)
              << std::endl;
    return;
  }
  if (!arrayIndices.empty())
  {
    if (adsDatatypeEntry.size % arrayIndices.size() != 0)
    {
      std::cerr << string_format("Size of %s is not divisible by the number of array indices: %lu",
                                 this->name, arrayIndices.size())
                << std::endl;
      return;
    }
    else
    {
      // We know the current entry we are building is an array type, so its name is something like:
      // ARRAY [0..99] OF INT, for example.
      // The type for this would be INT.
      // So in this case, we would need to look up INT in the raw index to get its entry.
      std::string typeStr = adsDatatypeEntry.type();
      auto it = datatypeEntryRawIndex.find(typeStr);
      if (it != datatypeEntryRawIndex.end())
      {
        uint32_t offset = 0;
        auto type = datatypeEntryRawIndex.at(typeStr);
        auto elementSize = type->size;
        for (const auto &arrayIndex : arrayIndices)
        {
          // We count the type at each array index of this entry as a child.
          // We append the index in square brackets because this is how the
          // name will be represented over ADS.
          auto child = std::make_shared<AdsDatatypeEntryExpanded>(*type,
                                                                  datatypeEntryRawIndex,
                                                                  this->indexOffsetFromRoot + offset,
                                                                  "[" + arrayIndex + "]");
          this->children.push_back(child);
          offset += elementSize;
          std::cout << "Found array: Name: " << child->name << " Type: " << child->type << "Offset: " << child->indexOffsetFromRoot << std::endl;
        }
      }
      else
      {
        std::cerr << string_format("Could not find type: [%s] in [%s] within the raw index of datatypes.\n",
                                   typeStr, adsDatatypeEntry.name())
                  << std::endl;
      }
    }
  }
}