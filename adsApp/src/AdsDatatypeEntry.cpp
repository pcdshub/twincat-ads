#include "AdsDatatypeEntry.h"
#include "AdsCodec.h"
#include "AdsDef.h"

#include <QJsonArray>
#include <QJsonObject>

const char * adsDatatypeIdToString(AdsDatatypeId type)
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

QString adsDatatypeFlagsToString(uint32_t flags)
{
  QStringList flagStrings;
  if (flags & ADSDATATYPEFLAG_DATATYPE)
    flagStrings << "DATATYPE";
  if (flags & ADSDATATYPEFLAG_DATAITEM)
    flagStrings << "DATAITEM";
  if (flags & ADSDATATYPEFLAG_REFERENCETO)
    flagStrings << "REFERENCETO";
  if (flags & ADSDATATYPEFLAG_METHODDEREF)
    flagStrings << "METHODDEREF";
  if (flags & ADSDATATYPEFLAG_OVERSAMPLE)
    flagStrings << "OVERSAMPLE";
  if (flags & ADSDATATYPEFLAG_BITVALUES)
    flagStrings << "BITVALUES";
  if (flags & ADSDATATYPEFLAG_PROPITEM)
    flagStrings << "PROPITEM";
  if (flags & ADSDATATYPEFLAG_TYPEGUID)
    flagStrings << "TYPEGUID";
  if (flags & ADSDATATYPEFLAG_PERSISTENT)
    flagStrings << "PERSISTENT";
  if (flags & ADSDATATYPEFLAG_COPYMASK)
    flagStrings << "COPYMASK";
  if (flags & ADSDATATYPEFLAG_TCCOMIFACEPTR)
    flagStrings << "TCCOMIFACEPTR";
  if (flags & ADSDATATYPEFLAG_METHODINFOS)
    flagStrings << "METHODINFOS";
  if (flags & ADSDATATYPEFLAG_ATTRIBUTES)
    flagStrings << "ATTRIBUTES";
  if (flags & ADSDATATYPEFLAG_ENUMINFOS)
    flagStrings << "ENUMINFOS";
  if (flags & ADSDATATYPEFLAG_ALIGNED)
    flagStrings << "ALIGNED";
  if (flags & ADSDATATYPEFLAG_STATIC)
    flagStrings << "STATIC";
  if (flags & ADSDATATYPEFLAG_SPLEVELS)
    flagStrings << "SPLEVELS";
  if (flags & ADSDATATYPEFLAG_IGNOREPERSIST)
    flagStrings << "IGNOREPERSIST";
  if (flags & ADSDATATYPEFLAG_ANYSIZEARRAY)
    flagStrings << "ANYSIZEARRAY";
  if (flags & ADSDATATYPEFLAG_PERSIST_DT)
    flagStrings << "PERSIST_DT";
  if (flags & ADSDATATYPEFLAG_INITONRESET)
    flagStrings << "INITONRESET";

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
    flagStrings << QString("UNKNOWN(0x%1)").arg(unknownFlags, 0, 16);
  }

  return flagStrings.join(" | ");
}

QJsonObject AdsDatatypeEntry::toJson() const
{
  auto codec = Ads::codec();

  QJsonObject jsonObject{
      {"hashValue", int(hashValue)},
      {"typeHashValue", int(typeHashValue)},
      {"size", int(size)},
      {"offs", int(offs)},
      {"dataType", int(dataType)},
      {"dataTypeName", adsDatatypeIdToString(AdsDatatypeId(dataType))},
      {"flags", int(flags)},
      {"flagsHex", QString::number(flags, 16)},
      {"flagsString", adsDatatypeFlagsToString(flags)},
      {"name", codec->toUnicode(name(), nameLength)},
      {"type", codec->toUnicode(type(), typeLength)},
      {"comment", codec->toUnicode(comment(), commentLength)},
  };

  if (arrayDim > 0)
  {
    QJsonArray arrayInfo;
    for (int i = 0; i < arrayDim; ++i)
    {
      const AdsDatatypeArrayInfo * info = this->arrayInfo() + i;
      QJsonObject arrayObj{
          {"lBound", int(info->lBound)},
          {"elements", int(info->elements)}};
      arrayInfo.append(arrayObj);
    }
    jsonObject["arrayInfo"] = arrayInfo;
  }

  if (subItemCount > 0)
  {
    QJsonArray subItemsArray;
    const AdsDatatypeEntry * entry = subItems();

    for (int i = 0; i < subItemCount; ++i)
    {
      subItemsArray.append(entry->toJson());
      entry = reinterpret_cast<const AdsDatatypeEntry *>(
          reinterpret_cast<const char *>(entry) + entry->entryLength);
    }
    jsonObject["subItems"] = subItemsArray;
  }

  return jsonObject;
}

AdsDatatypeEntry * AdsDatatypeStructItem(AdsDatatypeEntry * p,
                                         unsigned short iItem)
{
  uint16_t i;
  AdsDatatypeEntry * pItem;
  if (iItem >= p->subItemCount)
    return 0;
  pItem = (AdsDatatypeEntry *)(((unsigned char *)(p + 1)) + p->nameLength +
                               p->typeLength + p->commentLength + 3 +
                               p->arrayDim * sizeof(AdsDatatypeArrayInfo));
  for (i = 0; i < iItem; i++)
    pItem = (AdsDatatypeEntry *)(((unsigned char *)pItem) + pItem->entryLength);
  return pItem;
}
