#include "AdsSymbolIndex.h"
#include "AdsDatatypeEntry.h"
#include <sstream>

std::string AdsSymbolEntryAccess::adsSymbolFlagsToString(uint32_t flags)
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

void AdsSymbolIndex::build()
{
  auto end = mSymbolUpload.data() + mSymbolUpload.size();
  auto current = reinterpret_cast<const AdsSymbolEntryAccess *>(mSymbolUpload.data());

  while (reinterpret_cast<const char *>(current) < end)
  {
    auto maybeNext = current->maybeNext();
    if (reinterpret_cast<const char *>(maybeNext) > end)
    {
      printf("Datatype record extends past end of buffer. Skipped.");
      break;
    }
    auto currentName = current->name();
    mEntries.push_back(current);
    mNameIndex.insert(std::make_pair(currentName, current));
    current = maybeNext;
  }
}
