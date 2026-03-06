#include "AdsDatatypeIndex.h"
#include "AdsDatatypeEntry.h"
#include <assert.h>
#include <sstream>

AdsDatatypeIndex::~AdsDatatypeIndex()
{
  for (auto entry : mEntries)
  {
    delete entry;
  }
}

void AdsDatatypeIndex::build()
{
  auto end = mDataTypeUpload.data() + mDataTypeUpload.size();
  auto current = reinterpret_cast<const AdsDatatypeEntry *>(mDataTypeUpload.data());

  while (reinterpret_cast<const char *>(current) < end)
  {
    auto next = reinterpret_cast<const AdsDatatypeEntry *>(reinterpret_cast<const char *>(current) + current->entryLength);
    if (reinterpret_cast<const char *>(next) > end)
    {
      printf("Datatype record extends past end of buffer. Skipped.");
      break;
    }
    auto currentName = current->name();
    mNameRawIndex[currentName] = current;
    auto entry = new Entry(currentName, 0, current);
    mEntries.push_back(entry);
    mNameIndex[currentName] = entry;
    current = next;
  }
}

AdsDatatypeIndex::Entry::Entry(const std::string &name, uint32_t offset, const AdsDatatypeEntry *_adsType, const Entry *_parent)
    : mParent(_parent), mName(name), mOffset(offset), mAdsType(_adsType)
{
}

AdsDatatypeIndex::Entry::~Entry()
{
  for (auto child : mChildren)
  {
    delete child;
  }
}

// static
int AdsDatatypeIndex::Entry::arrayCount(const AdsDatatypeEntry *adsType, const AdsDatatypeIndex &index)
{
  if (adsType->arrayDim == 0)
    return 0;
  int count = 1;
  for (int iArrayDim = 0; iArrayDim < adsType->arrayDim; ++iArrayDim)
  {
    auto arrayInfo = adsType->arrayInfo()[iArrayDim];
    count *= arrayInfo.elements;
  }
  auto type = index.mNameRawIndex.at(adsType->type());
  if (!type)
  {
    printf("Unresolved type: [%s] in [%s]\n", adsType->type(), adsType->name());
  }
  else if (count * type->size != adsType->size)
  {
    return 0;
  }
  return count;
}

static std::list<std::string> expandArrayIndices(const AdsDatatypeEntry *adsType)
{
  std::list<std::string> arrayIndices;
  for (int iArrayDim = adsType->arrayDim - 1; iArrayDim >= 0; --iArrayDim)
  {
    auto arrayInfo = adsType->arrayInfo()[iArrayDim];
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

int AdsDatatypeIndex::Entry::childCount(const AdsDatatypeIndex &index) const
{
  if (mChildrenLoaded)
    return mChildren.size();

  std::string typeName = mAdsType->type();
  auto declaration = !typeName.empty() ? index.mNameRawIndex.at(typeName) : mAdsType;

  if (!declaration)
  {
    printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
    return 0;
  }

  return declaration->subItemCount + arrayCount(declaration, index);
}

std::list<const AdsDatatypeIndex::Entry *> AdsDatatypeIndex::Entry::children(const AdsDatatypeIndex &index)
{
  if (mChildrenLoaded)
    return mChildren;

  mChildrenLoaded = true;

  std::string typeName = mAdsType->type();
  auto declaration = !typeName.empty() ? index.mNameRawIndex.at(typeName) : mAdsType;

  if (!declaration)
  {
    printf("Unresolved type [%s] in [%s]\n", typeName.c_str(), mAdsType->name());
    return mChildren;
  }

  auto currentChild = declaration->subItems();
  for (int iChild = 0; iChild < declaration->subItemCount; ++iChild)
  {
    mChildren.push_back(new Entry(currentChild->name(), currentChild->offs, currentChild, this));
    currentChild = reinterpret_cast<const AdsDatatypeEntry *>(
        reinterpret_cast<const char *>(currentChild) + currentChild->entryLength);
  }

  auto arrayIndices = expandArrayIndices(declaration);
  if (arrayIndices.empty())
    return mChildren;

  if (declaration->size % arrayIndices.size() != 0)
  {
    printf("Size of %s is not divisible by the number of array indices: %lu", declaration->name(), arrayIndices.size());
    return mChildren;
  }
  auto itemSize = declaration->size / arrayIndices.size();
  auto offset = declaration->offs;
  if (declaration->offs)
  {
    printf("Offset of %s is not zero, but %u", declaration->name(), declaration->offs);
    assert(false);
  }
  for (const auto &arrayIndex : expandArrayIndices(declaration))
  {
    mChildren.push_back(new Entry("[" + arrayIndex + "]", offset, declaration, this));
    offset += itemSize;
  }

  assert(offset == declaration->size);

  return mChildren;
}

std::string AdsDatatypeIndex::Entry::fullName() const
{
  if (!mParent)
    return std::string(); // do not want the type name in the full name
  auto result = mParent->fullName();
  if (result.empty())
    return mName;
  if (mName[0] == '[')
    return result + name();
  return result + "." + name();
}

uint32_t AdsDatatypeIndex::Entry::offset() const
{
  return (mParent ? mParent->offset() : 0) + mOffset;
}
