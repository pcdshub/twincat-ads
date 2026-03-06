// AdsParseSymbols.h: interface for the CAdsParseSymbols class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ADSPARSESYMBOLS_H_
#define ADSPARSESYMBOLS_H_

#include "AdsDef.h"
#include "AdsLib.h"
#include <unordered_map>

///////////////////////////////////////////////////////////////////////////////
struct AdsSymbolEntryAccess : public AdsSymbolEntry
{
  const char * name() const { return reinterpret_cast<const char *>(this + 1); }
  const char * type() const { return name() + nameLength + 1; }
  const char * comment() const { return type() + typeLength + 1; }
  const AdsSymbolEntryAccess * maybeNext() const;
};

struct AdsDatatypeArrayInfo
{
	uint32_t lBound;   //
	uint32_t elements; //
};

struct AdsDatatypeEntry
{
	uint32_t entryLength;	// length of complete datatype entry
	uint32_t version;		// version of datatype structure
	uint32_t hashValue;		// hashValue of datatype to compare datatypes
	uint32_t typeHashValue; // hashValue of base type
	uint32_t size;			// size of datatype ( in bytes )
	uint32_t offs;			// offs of dataitem in parent datatype ( in bytes )
	uint32_t dataType;		// adsDataType of symbol (if alias)
	uint32_t flags;			//
	uint16_t nameLength;	// length of datatype name (excl. \0)
	uint16_t typeLength;	// length of dataitem type name (excl. \0)
	uint16_t commentLength; // length of comment (excl. \0)
	uint16_t arrayDim;		//
	uint16_t subItemCount;	//
							// dynamic part of the structure:
	// ADS_INT8    name[];             // name of datatype with terminating \0
	// ADS_INT8    type[];             // type name of dataitem with terminating \0
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
};

inline AdsDatatypeEntry *AdsDatatypeStructItem(AdsDatatypeEntry *p, uint16_t iItem)
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

class CAdsSymbolInfo
{
public:
	CAdsSymbolInfo() : m_pEntry(NULL) {};
	uint32_t iGrp;	   //
	uint32_t iOffs;	   //
	uint32_t size;	   // size of datatype ( in bytes )
	uint32_t offs;	   // offs of dataitem in parent datatype ( in bytes )
	uint32_t dataType; // adsDataType of symbol (if alias)
	uint32_t flags;	   //
	uint32_t entryLength;
	uint32_t nameLength;
	uint32_t typeLength;
	uint32_t commentLength;
	std::string name;
	std::string fullname;
	std::string type;
	std::string comment;
	AdsDatatypeEntry *m_pEntry;
};

struct AdsSymbolUploadInfo
{
	uint32_t nSymbols;
	uint32_t nSymSize;
};

struct AdsSymbolUploadInfo2
{
	uint32_t nSymbols = 0;
	uint32_t nSymSize = 0;
	uint32_t nDatatypes = 0;
	uint32_t nDatatypeSize = 0;
	uint32_t nMaxDynSymbols = 0;
	uint32_t nUsedDynSymbols = 0;
};

#define ADSIGRP_SYM_DT_UPLOAD 0xF00E
#define ADSIGRP_SYM_UPLOADINFO2 0xF00F

///////////////////////////////////////////////////////////////////////////////
// CAdsParseSymbols
class CAdsParseSymbols
{
public:
	CAdsParseSymbols(void *pSymbols, uint32_t nSymSize, void *pDatatypes = NULL, uint32_t nDTSize = 0);
	virtual ~CAdsParseSymbols();

	virtual uint32_t SymbolCount()
	{
		return m_nSymbols;
	}
	virtual uint32_t DatatypeCount()
	{
		return m_nDatatypes;
	}
	virtual AdsSymbolEntryAccess *Symbol(uint32_t sym)
	{
		return (sym < m_nSymbols) ? m_ppSymbolArray[sym] : NULL;
	}
	virtual bool Symbol(uint32_t sym, CAdsSymbolInfo &info);
	virtual const char *SymbolName(uint32_t sym) const
	{
		return (sym < m_nSymbols) ? m_ppSymbolArray[sym]->name() : NULL;
	}
	virtual const char *SymbolType(uint32_t sym) const
	{
		return (sym < m_nSymbols) ? m_ppSymbolArray[sym]->type() : NULL;
	}
	virtual const char *SymbolComment(uint32_t sym) const
	{
		return (sym < m_nSymbols) ? m_ppSymbolArray[sym]->comment() : NULL;
	}
	virtual uint32_t SubSymbolCount(uint32_t sym) const;
	virtual uint32_t SubSymbolCount(const char *sType) const;
	virtual uint32_t SubSymbolCount(const AdsDatatypeEntry *pEntry) const;
	virtual bool SubSymbolInfo(CAdsSymbolInfo main, uint32_t sub, CAdsSymbolInfo &info);

protected:
	virtual AdsDatatypeEntry *GetTypeByName(const std::string& sType);
	virtual const AdsDatatypeEntry *GetTypeByName(const std::string& sType) const;

	char *m_pSymbols;
	char *m_pDatatypes;
	uint32_t m_nSymbols;
	uint32_t m_nDatatypes;
	uint32_t m_nSymSize;
	uint32_t m_nDTSize;
	AdsSymbolEntryAccess **m_ppSymbolArray;
	AdsDatatypeEntry **m_ppDatatypeArray;
	char m_bufGetTypeByNameBuffer[300];
};

enum ADSGETDYNSYMBOLTYPE
{
	ADSDYNSYM_GET_NEXT = 1,
	ADSDYNSYM_GET_SIBLING = 2,
	ADSDYNSYM_GET_CHILD = 3,
	ADSDYNSYM_GET_PARENT = 4,
};

struct ADSDYNSYM_SUBINFO
{
	CAdsSymbolInfo infoParent;
	uint32_t nSub;
	ADSDYNSYM_SUBINFO *pParent;
};

class AdsSymbolParser
{
public:
	AdsSymbolParser(long amsClientPort, const AmsAddr &amsAddr);
	~AdsSymbolParser();

	long load(std::unordered_map<std::string, AdsSymbolEntry> &adsSymbolMap);
	long parent();
	long sibling();
	long child();
	long next();

	AdsSymbolEntry adsSymbolEntry;
	std::string name;
	std::string fullname;
	std::string type;
	std::string comment;

protected:
	CAdsParseSymbols *m_pDynSymbols;
	uint32_t m_nCurDynSymbol;
	ADSDYNSYM_SUBINFO *m_pCurSubSymbol;
	uint32_t m_nNextNavType;
	AmsAddr m_amsAddr;
	uint32_t m_amsClientPort;

	long AdsSetFirstDynSymbol(bool bForceReload);
	long AdsGetNextDynSymbol(uint32_t navType, std::string &strName, std::string &strFullName,
							 std::string &strType, std::string &strComment, uint32_t &adsType,
							 uint32_t &cbSymbolSize, uint32_t &nIndexGroup, uint32_t &IndexOffset);
};

#endif // ADSPARSESYMBOLS_H_
