#include "AdsSymbolIndex.h"
#include "AdsDatatypeEntry.h"
#include "AdsDatatypeIndex.h"

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
  this->entryLength = adsSymbolEntryRoot.entryLength;
  this->iGroup = adsSymbolEntryRoot.iGroup;
  this->iOffs = adsSymbolEntryRoot.iOffs;
  this->size = adsSymbolEntryRoot.size;
  this->dataType = adsSymbolEntryRoot.dataType;
  this->flags = adsSymbolEntryRoot.flags;
  this->nameLength = adsSymbolEntryRoot.nameLength;
  this->typeLength = adsSymbolEntryRoot.typeLength;
  this->commentLength = adsSymbolEntryRoot.commentLength;

  this->name = adsSymbolEntryRoot.name();
  this->type = adsSymbolEntryRoot.type();
  this->comment = adsSymbolEntryRoot.comment();
  this->flagStr = adsSymbolFlagsToString(flags);

  if (!this->name.empty() && this->name[0] == '[')
  {
    this->name = prefix + this->name;
  }
  else if (!prefix.empty())
  {
    this->name = prefix + "." + this->name;
  }

  auto it = datatypeEntryIndex.find(type);
  if (it == datatypeEntryIndex.end())
  {
    std::cout << "Failed to find " << type << " in the datatype entry index for " << name << std::endl;
    return;
  }
  auto datatypeEntry = datatypeEntryIndex.at(type);
  for (auto datatypeEntryChild : datatypeEntry->children)
  {
    auto newSymbolEntry = std::make_shared<AdsSymbolEntryExpanded>(*this,
                                                                   *datatypeEntryChild);
    children.push_back(newSymbolEntry);
  }
}

AdsSymbolEntryExpanded::AdsSymbolEntryExpanded(const AdsSymbolEntryExpanded &adsSymbolEntryExpandedParent,
                                               const AdsDatatypeEntryExpanded::Child &adsDatatypeEntryChild)
{
  this->entryLength = adsDatatypeEntryChild.entry->rawDatatypeEntry.entryLength;
  this->iGroup = adsSymbolEntryExpandedParent.iGroup;
  this->iOffs = adsSymbolEntryExpandedParent.iOffs + adsDatatypeEntryChild.iOffs;
  this->size = adsDatatypeEntryChild.entry->rawDatatypeEntry.size;
  this->dataType = adsDatatypeEntryChild.entry->rawDatatypeEntry.dataType;
  this->flags = adsDatatypeEntryChild.entry->rawDatatypeEntry.flags;
  this->nameLength = adsDatatypeEntryChild.entry->rawDatatypeEntry.nameLength;
  this->typeLength = adsDatatypeEntryChild.entry->rawDatatypeEntry.typeLength;
  this->commentLength = adsDatatypeEntryChild.entry->rawDatatypeEntry.commentLength;

  if (!adsDatatypeEntryChild.name.empty() && adsDatatypeEntryChild.name[0] == '[')
  {
    this->name = adsSymbolEntryExpandedParent.name + adsDatatypeEntryChild.name;
  }
  else
  {
    this->name = adsSymbolEntryExpandedParent.name + "." + adsDatatypeEntryChild.name;
  }

  this->type = adsDatatypeEntryChild.entry->typeName;
  this->comment = adsDatatypeEntryChild.entry->comment;
  this->flagStr = adsSymbolFlagsToString(flags);
  for (auto childDatatypeEntryExpanded : adsDatatypeEntryChild.entry->children)
  {
    auto newSymbolEntry = std::make_shared<AdsSymbolEntryExpanded>(*this,
                                                                   *childDatatypeEntryExpanded);
    children.push_back(newSymbolEntry);
  }
}

void AdsSymbolIndex::writeTree(std::ostream &buffer,
                               std::shared_ptr<AdsSymbolEntryExpanded> startingNode,
                               size_t numLevels,
                               size_t numTabs)
{
  if (!startingNode || numLevels < 1)
  {
    return;
  }

  std::string tabs;
  for (size_t tabNum = 0; tabNum < numTabs; tabNum++)
  {
    tabs += "\t";
  }

  buffer << tabs << "Name:   [" << startingNode->name << "]" << std::endl
         << tabs << "Type:   [" << startingNode->type << "]" << std::endl
         << tabs << "Group:  [" << startingNode->iGroup << "]" << std::endl
         << tabs << "Offset: [" << startingNode->iOffs << "]" << std::endl;

  if (startingNode->children.size() <= 0)
  {
    buffer << "--------------------" << std::endl;
    return;
  }

  for (auto child : startingNode->children)
  {
    writeTree(buffer, child, numLevels - 1, numTabs + 1);
  }
}