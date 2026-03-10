/**
 * @file AdsDatatypeEntry.h
 *
 * @brief Definition of the AdsDatatypeEntry structure and related types.
 *
 * This file contains the definition of the AdsDatatypeEntry structure, which is used to represent
 * the details of a data type in the ADS (Automation Device Specification) protocol.
 *
 * Found in https://github.com/zjw11525/MyRobot/blob/ebefb451d5b525fddcd080475813020efaece0e2/Matlab2CPP/Matlab2CPP/TcAdsDef.h
 *
 * Original copyright notice:
 * BECKHOFF-Industrieelektronik-GmbH
 */

#ifndef ADSDATATYPEENTRY_H_
#define ADSDATATYPEENTRY_H_

#include <cstdint>
#include <string>
#include <iostream>

#pragma pack(push, 1)

#define ADSDATATYPEFLAG_DATATYPE 1
#define ADSDATATYPEFLAG_DATAITEM 2
#define ADSDATATYPEFLAG_REFERENCETO 4
#define ADSDATATYPEFLAG_METHODDEREF 8
#define ADSDATATYPEFLAG_OVERSAMPLE 16
#define ADSDATATYPEFLAG_BITVALUES 32
#define ADSDATATYPEFLAG_PROPITEM 64
#define ADSDATATYPEFLAG_TYPEGUID 128
#define ADSDATATYPEFLAG_PERSISTENT 256
#define ADSDATATYPEFLAG_COPYMASK 512
#define ADSDATATYPEFLAG_TCCOMIFACEPTR 1024
#define ADSDATATYPEFLAG_METHODINFOS 2048
#define ADSDATATYPEFLAG_ATTRIBUTES 4096
#define ADSDATATYPEFLAG_ENUMINFOS 8192
#define ADSDATATYPEFLAG_ALIGNED 65536
#define ADSDATATYPEFLAG_STATIC 131072        // do not use offs
#define ADSDATATYPEFLAG_SPLEVELS 262144      // means "ContainSpLevelss" for DATATYPES and "HasSpLevels" for DATAITEMS
#define ADSDATATYPEFLAG_IGNOREPERSIST 524288 // do not restore persistent data
#define ADSDATATYPEFLAG_ANYSIZEARRAY 1048576 // if the index is exceeded, a value access to this array will return DeviceInvalidArrayIndex
#define ADSDATATYPEFLAG_PERSIST_DT 2097152   // data type used for persistent variables -> should be saved with persistent data
#define ADSDATATYPEFLAG_INITONRESET 4194304  // Persistent data will not be restored after reset (cold)

#define ADSDATATYPE_VERSION_NEWEST 0x00000001

struct AdsDatatypeArrayInfo
{
  uint32_t lBound;
  uint32_t elements;
};

enum class AdsDatatypeId : int
{
  Void = 0,
  Int8 = 16,
  UInt8 = 17,
  Int16 = 2,
  UInt16 = 18,
  Int32 = 3,
  UInt32 = 19,
  Int64 = 20,
  UInt64 = 21,
  Real32 = 4,
  Real64 = 5,
  BigType = 65,
  String = 30,
  WString = 31,
  Real80 = 32,
  Bit = 33,
  MaxTypes = 34
};

std::string adsDatatypeIdToString(AdsDatatypeId type);

struct AdsDatatypeEntry
{
  uint32_t entryLength;   // length of complete datatype entry
  uint32_t version;       // version of datatype structure
  uint32_t hashValue;     // hashValue of datatype to compare datatypes
  uint32_t typeHashValue; // hashValue of base type
  uint32_t size;          // size of datatype ( in bytes )
  uint32_t offs;          // offs of dataitem in parent datatype ( in bytes )
  uint32_t dataType;      // adsDataType of symbol (if alias)
  uint32_t flags;         //
  uint16_t nameLength;    // length of datatype name (excl. \0)
  uint16_t typeLength;    // length of dataitem type name (excl. \0)
  uint16_t commentLength; // length of comment (excl. \0)
  uint16_t arrayDim;      // array dimension and number of AdsDatatypeArrayInfo entries
  uint16_t subItemCount;  // number of sub items in this datatype
  // dynamic part of the structure:
  // ADS_INT8    name[];             // name of datatype with terminating \0.
  // For example: LCLS_MotionAbstraction.FB_ResetNC for a function block type
  // For example: ARRAY [1..1000] OF LCLS_Vacuum.PMPS.TcUnit.ST_AssertArrayResult for an array of structures type
  // For example: ARRAY [0..99] OF INT for an array of a base type
  //
  // ADS_INT8    type[];             // type name of dataitem with terminating \0.
  // For example: UDINT for a UDINT Enum, blank for a structured type.
  // For example: LCLS_Vacuum.PMPS.TcUnit.ST_AssertArrayResult for a structure type in each index of an array type
  // For example: INT for a base type in an array of base types
  //
  // ADS_INT8    comment[];          // comment of datatype with terminating \0
  // AdsDatatypeArrayInfo  array[];  // array information, if arrayDim > 0
  // AdsDatatypeEntry    subItems[]; // sub items, if subItems > 0
  const char *name() const
  {
    return reinterpret_cast<const char *>(this + 1);
  }
  const char *type() const
  {
    return name() + nameLength + 1;
  }
  const char *comment() const
  {
    return type() + typeLength + 1;
  }
  const AdsDatatypeArrayInfo *arrayInfo() const
  {
    return reinterpret_cast<const AdsDatatypeArrayInfo *>(comment() + commentLength + 1);
  }
  const AdsDatatypeEntry *subItems() const
  {
    return reinterpret_cast<const AdsDatatypeEntry *>(reinterpret_cast<const char *>(arrayInfo()) + arrayDim * sizeof(AdsDatatypeArrayInfo));
  }
  void print() const
  {
      std::cout << "sizeof(AdsDatatypeEntry): [" << sizeof(AdsDatatypeEntry) << "]" << std::endl;
      std::cout << "uint32_t entryLength;   : [" << entryLength    << "]" << std::endl;
      std::cout << "uint32_t version;       : [" << version        << "]" << std::endl;
      std::cout << "uint32_t hashValue;     : [" << hashValue      << "]" << std::endl;
      std::cout << "uint32_t typeHashValue; : [" << typeHashValue  << "]" << std::endl;
      std::cout << "uint32_t size;          : [" << size           << "]" << std::endl;
      std::cout << "uint32_t offs;          : [" << offs           << "]" << std::endl;
      std::cout << "uint32_t dataType;      : [" << dataType       << "]" << std::endl;
      std::cout << "uint32_t flags;         : [" << flags          << "]" << std::endl;
      std::cout << "uint16_t nameLength;    : [" << nameLength     << "]" << std::endl;
      std::cout << "uint16_t typeLength;    : [" << typeLength     << "]" << std::endl;
      std::cout << "uint16_t commentLength; : [" << commentLength  << "]" << std::endl;
      std::cout << "uint16_t arrayDim;      : [" << arrayDim       << "]" << std::endl;
      std::cout << "uint16_t subItemCount;  : [" << subItemCount   << "]" << std::endl;
  }
};

AdsDatatypeEntry *AdsDatatypeStructItem(AdsDatatypeEntry *p,
                                        unsigned short iItem);

                                        
std::string adsDatatypeFlagsToString(uint32_t flags);

#pragma pack(pop)

#endif // ADSDATATYPEENTRY_H_