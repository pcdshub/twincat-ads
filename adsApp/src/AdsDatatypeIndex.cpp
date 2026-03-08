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
  std::cout << "Building datatype index..." << std::endl;

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
    std::string currentTypeName = current->name();
    mDatatypeEntryRawIndex[currentTypeName] = current;
    current = next;
  }
  for (auto pair : mDatatypeEntryRawIndex)
  {
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
                                                   uint32_t startingOffset, const std::string &prefix) : AdsDatatypeEntry(adsDatatypeEntry)
{
  flagStr = adsDatatypeFlagsToString(flags);
  this->name = adsDatatypeEntry.name();
  if (!prefix.empty())
  {
    this->name = prefix + "." + this->name;
  }
  this->type = adsDatatypeEntry.type();
  std::cout << "Name: " << name << "Type: " << type << std::endl;
  this->comment = adsDatatypeEntry.comment();

  this->indexOffsetFromRoot = startingOffset + adsDatatypeEntry.offs;

  // Let's handle the case of this datatype being an array.
  this->numArrayElements = AdsDatatypeIndex::getNumberOfElementsInArray(adsDatatypeEntry, datatypeEntryRawIndex);
  auto arrayIndices = AdsDatatypeIndex::expandArrayIndices(adsDatatypeEntry);
  if (this->numArrayElements != arrayIndices.size())
  {
    std::cerr << string_format("Number of array indices and number of elements did not match for %s of type %s.",
                               this->name, this->type)
              << std::endl;
  }
  if (!arrayIndices.empty())
  {
    if (adsDatatypeEntry.size % arrayIndices.size() != 0)
    {
      std::cerr << string_format("Size of %s is not divisible by the number of array indices: %lu",
                                 this->name, arrayIndices.size())
                << std::endl;
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
        }
        // Exit for now, still have to handle struct case but let's see what these array indices look like so far.
        exit(0);
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

// int AdsDatatypeIndex::Entry::childCount(const AdsDatatypeIndex &index) const
// {
//   if (mChildrenLoaded)
//     return mChildren.size();

//   std::string typeName = mAdsType->type();
//   auto declaration = mAdsType;
//   if (!typeName.empty())
//   {
//     auto it = index.mDatatypeEntryRawIndex.find(typeName);
//     if (it == index.mDatatypeEntryRawIndex.end())
//     {
//       printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
//       return 0;
//     }
//     declaration = index.mDatatypeEntryRawIndex.at(typeName);
//     if (!declaration)
//     {
//       printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
//       return 0;
//     }
//   }

//   return declaration->subItemCount + arrayCount(declaration, index);
// }

// std::list<AdsDatatypeIndex::Entry *> AdsDatatypeIndex::Entry::children(const AdsDatatypeIndex &index)
// {
//   if (mChildrenLoaded)
//     return mChildren;

//   mChildrenLoaded = true;

//   std::string typeName = mAdsType->type();

//   auto declaration = mAdsType;
//   if (!typeName.empty())
//   {
//     auto it = index.mDatatypeEntryRawIndex.find(typeName);
//     if (it == index.mDatatypeEntryRawIndex.end())
//     {
//       printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
//       return mChildren;
//     }
//     declaration = index.mDatatypeEntryRawIndex.at(typeName);
//     if (!declaration)
//     {
//       printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
//       return mChildren;
//     }
//   }
//   auto currentChild = declaration->subItems();
//   for (int iChild = 0; iChild < declaration->subItemCount; ++iChild)
//   {
//     mChildren.push_back(new Entry(currentChild->name(), currentChild->offs, currentChild, this));
//     currentChild = reinterpret_cast<const AdsDatatypeEntry *>(
//         reinterpret_cast<const char *>(currentChild) + currentChild->entryLength);
//   }

//   auto arrayIndices = expandArrayIndices(declaration);
//   if (arrayIndices.empty())
//     return mChildren;

//   if (declaration->size % arrayIndices.size() != 0)
//   {
//     printf("Size of %s is not divisible by the number of array indices: %lu", declaration->name(), arrayIndices.size());
//     return mChildren;
//   }
//   auto itemSize = declaration->size / arrayIndices.size();
//   auto offset = declaration->offs;
//   if (declaration->offs)
//   {
//     printf("Offset of %s is not zero, but %u", declaration->name(), declaration->offs);
//     assert(false);
//   }
//   for (const auto &arrayIndex : expandArrayIndices(declaration))
//   {
//     mChildren.push_back(new Entry("[" + arrayIndex + "]", offset, declaration, this));
//     offset += itemSize;
//   }

//   assert(offset == declaration->size);

//   return mChildren;
// }