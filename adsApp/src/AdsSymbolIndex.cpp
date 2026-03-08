#include "AdsSymbolIndex.h"
#include "AdsDatatypeEntry.h"
#include "AdsDatatypeIndex.h"
#include <sstream>

std::string adsSymbolFlagsToString(uint32_t flags)
{
  std::string flagString;
  if (flags & ADSSYMBOLFLAG_PERSISTENT)
    flagString += "|PERSISTENT";
  if (flags & ADSSYMBOLFLAG_BITVALUE)
    flagString += "|BITVALUE";
  if (flags & ADSSYMBOLFLAG_REFERENCETO)
    flagString += "|REFERENCETO";
  if (flags & ADSSYMBOLFLAG_TYPEGUID)
    flagString += "|TYPEGUID";
  if (flags & ADSSYMBOLFLAG_TCCOMIFACEPTR)
    flagString += "|TCCOMIFACEPTR";
  if (flags & ADSSYMBOLFLAG_READONLY)
    flagString += "|READONLY";
  if (flags & ADSSYMBOLFLAG_CONTEXTMASK)
    flagString += "|CONTEXTMASK";
  if (flagString.length() > 0)
    flagString.erase(0, 1);
  return flagString;
}

AdsSymbolIndex::AdsSymbolIndex(const std::vector<char> &symbolUpload,
                               const AdsDatatypeIndex &adsDatatypeIndex)
    : mSymbolUpload(symbolUpload), mAdsDatatypeIndex(adsDatatypeIndex)
{
  auto end = mSymbolUpload.data() + mSymbolUpload.size();
  auto current = reinterpret_cast<const AdsSymbolEntryAccess *>(mSymbolUpload.data());

  while (reinterpret_cast<const char *>(current) < end)
  {
    auto maybeNext = current->maybeNext();
    if (reinterpret_cast<const char *>(maybeNext) > end)
    {
      std::cerr << "The symbol record retrieved extends past the end of the buffer. Some symbols may be lost." << std::endl;
      break;
    }
    std::string currentName = current->name();
    mSymbolEntryRawIndex.insert(std::make_pair(currentName, current));
    current = maybeNext;
  }
  for (auto pair : mSymbolEntryRawIndex)
  {
    mSymbolEntryIndex.insert(std::make_pair(
        pair.first,
        std::make_shared<AdsSymbolEntryExpanded>(*pair.second,
                                                 adsDatatypeIndex.getDatatypeEntryIndex(),
                                                 "")));
  }
}

const std::unordered_map<std::string, const std::shared_ptr<AdsSymbolEntryExpanded>> &AdsSymbolIndex::getSymbolEntryIndex() const
{
  return mSymbolEntryIndex;
}

AdsSymbolEntryExpanded::AdsSymbolEntryExpanded(const AdsSymbolEntryAccess &adsSymbolEntryRoot,
                                               const std::unordered_map<std::string, std::shared_ptr<AdsDatatypeEntryExpanded>> &datatypeEntryIndex,
                                               const std::string &prefix)
{
  entryLength = adsSymbolEntryRoot.entryLength;
  iGroup = adsSymbolEntryRoot.iGroup;
  iOffs = adsSymbolEntryRoot.iOffs;
  size = adsSymbolEntryRoot.size;
  dataType = adsSymbolEntryRoot.dataType;
  flags = adsSymbolEntryRoot.flags;
  nameLength = adsSymbolEntryRoot.nameLength;
  typeLength = adsSymbolEntryRoot.typeLength;
  commentLength = adsSymbolEntryRoot.commentLength;

  name = adsSymbolEntryRoot.name();
  type = adsSymbolEntryRoot.type();
  comment = adsSymbolEntryRoot.comment();
  flagStr = adsSymbolFlagsToString(flags);
  if (!prefix.empty())
  {
    name = prefix + "." + name;
  }

  auto it = datatypeEntryIndex.find(type);
  if (it == datatypeEntryIndex.end())
  {
    std::cerr << "Failed to find " << type << " in the datatype entry index for " << name << std::endl;
    return;
  }
  auto datatypeEntry = datatypeEntryIndex.at(type);
  for (auto child : datatypeEntry->children)
  {
    auto newSymbolEntry = std::make_shared<AdsSymbolEntryExpanded>(*this,
                                                                   *child);
    children.push_back(newSymbolEntry);
  }
}

AdsSymbolEntryExpanded::AdsSymbolEntryExpanded(const AdsSymbolEntryExpanded &adsSymbolEntryRoot,
                                               const AdsDatatypeEntryExpanded &adsDatatypeEntry)
{
  entryLength = adsDatatypeEntry.entryLength;
  iGroup = adsSymbolEntryRoot.iGroup;
  iOffs = adsSymbolEntryRoot.iOffs + adsDatatypeEntry.indexOffsetFromRoot;
  size = adsDatatypeEntry.size;
  dataType = adsDatatypeEntry.dataType;
  flags = adsDatatypeEntry.flags;
  nameLength = adsDatatypeEntry.nameLength;
  typeLength = adsDatatypeEntry.typeLength;
  commentLength = adsDatatypeEntry.commentLength;

  name = adsDatatypeEntry.name;
  type = adsDatatypeEntry.type;
  comment = adsDatatypeEntry.comment;
  flagStr = adsSymbolFlagsToString(flags);
  if (!adsSymbolEntryRoot.name.empty())
  {
    name = adsSymbolEntryRoot.name + "." + name;
  }
  for (auto datatypeEntry : adsDatatypeEntry.children)
  {
    auto newSymbolEntry = std::make_shared<AdsSymbolEntryExpanded>(adsSymbolEntryRoot,
                                                                   *datatypeEntry);
    children.push_back(newSymbolEntry);
  }
}

void AdsSymbolIndex::print(std::shared_ptr<AdsSymbolEntryExpanded> startingNode, size_t numLevels)
{
  if (!startingNode || numLevels < 1)
    std::cout << "----------" << std::endl;
  return;

  std::cout << "Name:   [" << startingNode->name << "]" << std::endl
            << "Type:   [" << startingNode->type << "]" << std::endl
            << "Group:  [" << startingNode->iGroup << "]" << std::endl
            << "Offset: [" << startingNode->iOffs << "]" << std::endl;

  for (auto child : startingNode->children)
  {
    for (size_t numTabs = 0; numTabs < numLevels - 1; numTabs++)
    {
      std::cout << "\t";
    }
    print(child, numLevels - 1);
  }
}