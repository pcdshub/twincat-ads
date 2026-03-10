#include "AdsDatatypeEntry.h"
#include "AdsDef.h"

std::string adsDatatypeIdToString(AdsDatatypeId type)
{
  switch (type)
  {
  case AdsDatatypeId::Void:
    return "Void";
  case AdsDatatypeId::Int8:
    return "Int8";
  case AdsDatatypeId::UInt8:
    return "UInt8";
  case AdsDatatypeId::Int16:
    return "Int16";
  case AdsDatatypeId::UInt16:
    return "UInt16";
  case AdsDatatypeId::Int32:
    return "Int32";
  case AdsDatatypeId::UInt32:
    return "UInt32";
  case AdsDatatypeId::Int64:
    return "Int64";
  case AdsDatatypeId::UInt64:
    return "UInt64";
  case AdsDatatypeId::Real32:
    return "Real32";
  case AdsDatatypeId::Real64:
    return "Real64";
  case AdsDatatypeId::BigType:
    return "BigType";
  case AdsDatatypeId::String:
    return "String";
  case AdsDatatypeId::WString:
    return "WString";
  case AdsDatatypeId::Real80:
    return "Real80";
  case AdsDatatypeId::Bit:
    return "Bit";
  default:
    return "Unknown";
  }
}

std::string adsDatatypeFlagsToString(uint32_t flags)
{
  std::string flagString;
  if (flags & ADSDATATYPEFLAG_DATATYPE)
    flagString += "|DATATYPE";
  if (flags & ADSDATATYPEFLAG_DATAITEM)
    flagString += "|DATAITEM";
  if (flags & ADSDATATYPEFLAG_REFERENCETO)
    flagString += "|REFERENCETO";
  if (flags & ADSDATATYPEFLAG_METHODDEREF)
    flagString += "|METHODDEREF";
  if (flags & ADSDATATYPEFLAG_OVERSAMPLE)
    flagString += "|OVERSAMPLE";
  if (flags & ADSDATATYPEFLAG_BITVALUES)
    flagString += "|BITVALUES";
  if (flags & ADSDATATYPEFLAG_PROPITEM)
    flagString += "|PROPITEM";
  if (flags & ADSDATATYPEFLAG_TYPEGUID)
    flagString += "|TYPEGUID";
  if (flags & ADSDATATYPEFLAG_PERSISTENT)
    flagString += "|PERSISTENT";
  if (flags & ADSDATATYPEFLAG_COPYMASK)
    flagString += "|COPYMASK";
  if (flags & ADSDATATYPEFLAG_TCCOMIFACEPTR)
    flagString += "|TCCOMIFACEPTR";
  if (flags & ADSDATATYPEFLAG_METHODINFOS)
    flagString += "|METHODINFOS";
  if (flags & ADSDATATYPEFLAG_ATTRIBUTES)
    flagString += "|ATTRIBUTES";
  if (flags & ADSDATATYPEFLAG_ENUMINFOS)
    flagString += "|ENUMINFOS";
  if (flags & ADSDATATYPEFLAG_ALIGNED)
    flagString += "|ALIGNED";
  if (flags & ADSDATATYPEFLAG_STATIC)
    flagString += "|STATIC";
  if (flags & ADSDATATYPEFLAG_SPLEVELS)
    flagString += "|SPLEVELS";
  if (flags & ADSDATATYPEFLAG_IGNOREPERSIST)
    flagString += "|IGNOREPERSIST";
  if (flags & ADSDATATYPEFLAG_ANYSIZEARRAY)
    flagString += "|ANYSIZEARRAY";
  if (flags & ADSDATATYPEFLAG_PERSIST_DT)
    flagString += "|PERSIST_DT";
  if (flags & ADSDATATYPEFLAG_INITONRESET)
    flagString += "|INITONRESET";

  uint32_t unknownFlags = flags & ~(
                                      ADSDATATYPEFLAG_DATATYPE | ADSDATATYPEFLAG_DATAITEM | ADSDATATYPEFLAG_REFERENCETO |
                                      ADSDATATYPEFLAG_METHODDEREF | ADSDATATYPEFLAG_OVERSAMPLE | ADSDATATYPEFLAG_BITVALUES |
                                      ADSDATATYPEFLAG_PROPITEM | ADSDATATYPEFLAG_TYPEGUID | ADSDATATYPEFLAG_PERSISTENT |
                                      ADSDATATYPEFLAG_COPYMASK | ADSDATATYPEFLAG_TCCOMIFACEPTR | ADSDATATYPEFLAG_METHODINFOS |
                                      ADSDATATYPEFLAG_ATTRIBUTES | ADSDATATYPEFLAG_ENUMINFOS | ADSDATATYPEFLAG_ALIGNED |
                                      ADSDATATYPEFLAG_STATIC | ADSDATATYPEFLAG_SPLEVELS | ADSDATATYPEFLAG_IGNOREPERSIST |
                                      ADSDATATYPEFLAG_ANYSIZEARRAY | ADSDATATYPEFLAG_PERSIST_DT | ADSDATATYPEFLAG_INITONRESET);
  if (unknownFlags)
  {
    flagString += "|UNKNOWN";
  }

  if (!flagString.empty())
    flagString.erase(0, 1);

  return flagString;
}

AdsDatatypeEntry *AdsDatatypeStructItem(AdsDatatypeEntry *p,
                                        unsigned short iItem)
{
  uint16_t i;
  AdsDatatypeEntry *pItem;
  if (iItem >= p->subItemCount)
    return 0;
  pItem = (AdsDatatypeEntry *)(((unsigned char *)(p + 1)) + p->nameLength +
                               p->typeLength + p->commentLength + 3 +
                               p->arrayDim * sizeof(AdsDatatypeArrayInfo));
  for (i = 0; i < iItem; i++)
    pItem = (AdsDatatypeEntry *)(((unsigned char *)pItem) + pItem->entryLength);
  return pItem;
}
