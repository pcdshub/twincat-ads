// AdsParseSymbols.h: interface for the CAdsParseSymbols class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_ADSPARSESYMBOLS_H__353EFDFE_6136_400C_A0E7_B24C2480E9F1__INCLUDED_)
#define AFX_ADSPARSESYMBOLS_H__353EFDFE_6136_400C_A0E7_B24C2480E9F1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "AdsDef.h"
#include "AdsLib.h"
#include <unordered_map>

///////////////////////////////////////////////////////////////////////////////
typedef struct
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
	uint16_t subItems;		//
} AdsDatatypeEntry;

#define PADSDATATYPENAME(p) ((char *)(((AdsDatatypeEntry *)p) + 1))
#define PADSDATATYPETYPE(p) (((char *)(((AdsDatatypeEntry *)p) + 1)) + ((AdsDatatypeEntry *)p)->nameLength + 1)
#define PADSDATATYPECOMMENT(p) (((char *)(((AdsDatatypeEntry *)p) + 1)) + ((AdsDatatypeEntry *)p)->nameLength + 1 + ((AdsDatatypeEntry *)p)->typeLength + 1)
#define PADSDATATYPEARRAYINFO(p) (AdsDatatypeArrayInfo *)(((char *)(((AdsDatatypeEntry *)p) + 1)) + ((AdsDatatypeEntry *)p)->nameLength + 1 + ((AdsDatatypeEntry *)p)->typeLength + 1 + ((AdsDatatypeEntry *)p)->commentLength + 1)

inline AdsDatatypeEntry *AdsDatatypeStructItem(AdsDatatypeEntry *p, uint16_t iItem)
{
	uint16_t i;
	AdsDatatypeEntry *pItem;
	if (iItem >= p->subItems)
		return NULL;
	pItem = (AdsDatatypeEntry *)(((uint8_t *)(p + 1)) + p->nameLength + p->typeLength + p->commentLength + 3 + p->arrayDim * sizeof(AdsDatatypeArrayInfo));
	for (i = 0; i < iItem; i++)
		pItem = (AdsDatatypeEntry *)(((uint8_t *)pItem) + pItem->entryLength);
	return pItem;
}

typedef struct
{
	uint32_t lBound;   //
	uint32_t elements; //
} AdsDatatypeArrayInfo;

class CAdsSymbolInfo
{
public:
	CAdsSymbolInfo() : m_pEntry(NULL) {};
	unsigned long iGrp;		//
	unsigned long iOffs;	//
	unsigned long size;		// size of datatype ( in bytes )
	unsigned long offs;		// offs of dataitem in parent datatype ( in bytes )
	unsigned long dataType; // adsDataType of symbol (if alias)
	unsigned long flags;	//
	unsigned long entryLength;
	unsigned long nameLength;
	unsigned long typeLength;
	unsigned long commentLength;
	std::string name;
	std::string fullname;
	std::string type;
	std::string comment;
	AdsDatatypeEntry *m_pEntry;
};

#define PADSSYMBOLNAME(p) ((char *)(((AdsSymbolEntry *)p) + 1))
#define PADSSYMBOLTYPE(p) (((char *)(((AdsSymbolEntry *)p) + 1)) + ((AdsSymbolEntry *)p)->nameLength + 1)
#define PADSSYMBOLCOMMENT(p) (((char *)(((AdsSymbolEntry *)p) + 1)) + ((AdsSymbolEntry *)p)->nameLength + 1 + ((AdsSymbolEntry *)p)->typeLength + 1)

#define PADSNEXTSYMBOLENTRY(pEntry) (*((uint32_t *)(((char *)pEntry) + ((AdsSymbolEntry *)pEntry)->entryLength))            \
										 ? ((AdsSymbolEntry *)(((char *)pEntry) + ((AdsSymbolEntry *)pEntry)->entryLength)) \
										 : NULL)

typedef struct
{
	uint32_t nSymbols;
	uint32_t nSymSize;
} AdsSymbolUploadInfo;

typedef struct
{
	unsigned long nSymbols;
	unsigned long nSymSize;
	unsigned long nDatatypes;
	unsigned long nDatatypeSize;
	unsigned long nMaxDynSymbols;
	unsigned long nUsedDynSymbols;
} AdsSymbolUploadInfo2;

#define ADSIGRP_SYM_DT_UPLOAD 0xF00E
#define ADSIGRP_SYM_UPLOADINFO2 0xF00F

///////////////////////////////////////////////////////////////////////////////
// CAdsParseSymbols
class CAdsParseSymbols
{
public:
	CAdsParseSymbols(void *pSymbols, unsigned int nSymSize, void *pDatatypes = NULL, unsigned int nDTSize = 0);
	virtual ~CAdsParseSymbols();

	virtual unsigned int SymbolCount()
	{
		return m_nSymbols;
	}
	virtual unsigned int DatatypeCount()
	{
		return m_nDatatypes;
	}
	virtual AdsSymbolEntry *Symbol(unsigned int sym)
	{
		return (sym < m_nSymbols) ? m_ppSymbolArray[sym] : NULL;
	}
	virtual bool Symbol(unsigned int sym, CAdsSymbolInfo &info);
	virtual char *SymbolName(unsigned int sym)
	{
		return (sym < m_nSymbols) ? PADSSYMBOLNAME(m_ppSymbolArray[sym]) : NULL;
	}
	virtual char *SymbolType(unsigned int sym)
	{
		return (sym < m_nSymbols) ? PADSSYMBOLTYPE(m_ppSymbolArray[sym]) : NULL;
	}
	virtual char *SymbolComment(unsigned int sym)
	{
		return (sym < m_nSymbols) ? PADSSYMBOLCOMMENT(m_ppSymbolArray[sym]) : NULL;
	}
	virtual unsigned int SubSymbolCount(unsigned int sym);
	virtual unsigned int SubSymbolCount(char *sType);
	virtual unsigned int SubSymbolCount(AdsDatatypeEntry *pEntry);
	virtual bool SubSymbolInfo(CAdsSymbolInfo main, unsigned int sub, CAdsSymbolInfo &info);

protected:
	virtual AdsDatatypeEntry *GetTypeByName(std::string sType);

	char *m_pSymbols;
	char *m_pDatatypes;
	unsigned int m_nSymbols;
	unsigned int m_nDatatypes;
	unsigned int m_nSymSize;
	unsigned int m_nDTSize;
	AdsSymbolEntry **m_ppSymbolArray;
	AdsDatatypeEntry **m_ppDatatypeArray;
	char m_bufGetTypeByNameBuffer[300];
};

typedef enum ADSGETDYNSYMBOLTYPE
{
	ADSDYNSYM_GET_NEXT = 1,
	ADSDYNSYM_GET_SIBLING = 2,
	ADSDYNSYM_GET_CHILD = 3,
	ADSDYNSYM_GET_PARENT = 4,
} ADSGETDYNSYMBOLTYPE;

typedef struct ADSDYNSYM_SUBINFO
{
	CAdsSymbolInfo infoParent;
	long nSub;
	ADSDYNSYM_SUBINFO *pParent;
} ADSDYNSYM_SUBINFO;

class AdsSymbolParser
{
public:
	AdsSymbolParser(long amsClientPort, const AmsAddr &amsAddr);
	~AdsSymbolParser();

	long load(std::unordered_map<std::string, AdsSymbolEntry>& adsSymbolMap);
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
	long m_nCurDynSymbol;
	ADSDYNSYM_SUBINFO *m_pCurSubSymbol;
	long m_nNextNavType;
	AmsAddr m_amsAddr;
	long m_amsClientPort;

	long AdsSetFirstDynSymbol(bool bForceReload);
	long AdsGetNextDynSymbol(long navType, std::string &strName, std::string &strFullName,
							 std::string &strType, std::string &strComment, uint32_t &adsType,
							 uint32_t &cbSymbolSize, uint32_t &nIndexGroup, uint32_t &IndexOffset);
};

#endif
