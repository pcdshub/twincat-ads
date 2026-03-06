// AdsParseSymbols.cpp: implementation of the CAdsParseSymbols class.
//
//////////////////////////////////////////////////////////////////////

#include "AdsParseSymbols.h"
#include "adsAsynPortDriverUtils.h"
#include <assert.h>
#include <cstring>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// CAdsParseSymbols

CAdsParseSymbols::CAdsParseSymbols(void *pSymbols, unsigned int nSymSize, void *pDatatypes, unsigned int nDTSize)
{
	memset(m_bufGetTypeByNameBuffer, 0, sizeof(m_bufGetTypeByNameBuffer));
	m_pSymbols = new char[nSymSize + sizeof(unsigned long)];
	if (m_pSymbols)
	{
		m_nSymSize = nSymSize;
		if (nSymSize)
			memcpy(m_pSymbols, pSymbols, nSymSize);
		*(unsigned long *)&m_pSymbols[nSymSize] = 0;
		m_nSymbols = 0;
		unsigned int offs = 0;
		while (*(unsigned long *)&m_pSymbols[offs])
		{
			m_nSymbols++;
			offs += *(unsigned long *)&m_pSymbols[offs];
		}
		assert(offs == nSymSize);
		m_ppSymbolArray = new AdsSymbolEntryAccess *[m_nSymbols];
		m_nSymbols = offs = 0;
		while (*(unsigned long *)&m_pSymbols[offs])
		{
			m_ppSymbolArray[m_nSymbols++] = (AdsSymbolEntryAccess *)&m_pSymbols[offs];
			offs += *(unsigned long *)&m_pSymbols[offs];
		}
		assert(offs == nSymSize);
	}
	m_pDatatypes = new char[nDTSize + sizeof(unsigned long)];
	if (m_pDatatypes)
	{
		m_nDTSize = nDTSize;
		if (nDTSize)
			memcpy(m_pDatatypes, pDatatypes, nDTSize);
		*(unsigned long *)&m_pDatatypes[nDTSize] = 0;
		m_nDatatypes = 0;
		unsigned int offs = 0;
		while (*(unsigned long *)&m_pDatatypes[offs])
		{
			m_nDatatypes++;
			offs += *(unsigned long *)&m_pDatatypes[offs];
		}
		assert(offs == nDTSize);
		m_ppDatatypeArray = new AdsDatatypeEntry *[m_nDatatypes];
		m_nDatatypes = offs = 0;
		while (*(unsigned long *)&m_pDatatypes[offs])
		{
			m_ppDatatypeArray[m_nDatatypes++] = (AdsDatatypeEntry *)&m_pDatatypes[offs];
			offs += *(unsigned long *)&m_pDatatypes[offs];
		}
		assert(offs == nDTSize);
	}
}

///////////////////////////////////////////////////////////////////////////////
CAdsParseSymbols::~CAdsParseSymbols()
{
	if (m_pSymbols)
		delete m_pSymbols;
	if (m_pDatatypes)
		delete m_pDatatypes;
	if (m_ppSymbolArray)
		delete m_ppSymbolArray;
	if (m_ppDatatypeArray)
		delete m_ppDatatypeArray;
}

static int CompareDTByName(const void *p1, const void *p2)
{
	return strcmp((char *)((*(AdsDatatypeEntry **)p1) + 1), (char *)((*(AdsDatatypeEntry **)p2) + 1));
}

const AdsDatatypeEntry *CAdsParseSymbols::GetTypeByName(const std::string &sType) const
{
	AdsDatatypeEntry *pKey = (AdsDatatypeEntry *)m_bufGetTypeByNameBuffer;
	strcpy((char *)(pKey + 1), sType.c_str());

	// serach data type by name
	AdsDatatypeEntry **ppEntry = (AdsDatatypeEntry **)bsearch(&pKey, m_ppDatatypeArray, m_nDatatypes,
															  sizeof(*m_ppDatatypeArray), CompareDTByName);

	if (ppEntry)
		return *ppEntry;
	else
		return NULL;
}

AdsDatatypeEntry *CAdsParseSymbols::GetTypeByName(const std::string &sType)
{
	AdsDatatypeEntry *pKey = (AdsDatatypeEntry *)m_bufGetTypeByNameBuffer;
	strcpy((char *)(pKey + 1), sType.c_str());

	// serach data type by name
	AdsDatatypeEntry **ppEntry = (AdsDatatypeEntry **)bsearch(&pKey, m_ppDatatypeArray, m_nDatatypes,
															  sizeof(*m_ppDatatypeArray), CompareDTByName);

	if (ppEntry)
		return *ppEntry;
	else
		return NULL;
}

uint32_t CAdsParseSymbols::SubSymbolCount(unsigned int sym) const
{
	if (sym < m_nSymbols)
		return SubSymbolCount(SymbolType(sym));
	else
		return 0;
}

uint32_t CAdsParseSymbols::SubSymbolCount(const char *sType) const
{
	auto pEntry = GetTypeByName(sType);
	if (pEntry)
		return SubSymbolCount(pEntry);
	else
		return 0;
}

uint32_t CAdsParseSymbols::SubSymbolCount(const AdsDatatypeEntry *pEntry) const
{
	uint32_t cnt = 0;
	if (pEntry)
	{
		if (pEntry->subItemCount)
		{
			cnt += pEntry->subItemCount;
		}
		else if (pEntry->arrayDim)
		{
			cnt = 1;
			auto pAI = pEntry->arrayInfo();
			for (unsigned short i = 0; i < pEntry->arrayDim; i++)
				cnt *= pAI[i].elements;
		}
	}

	return cnt;
}

bool CAdsParseSymbols::Symbol(unsigned int sym, CAdsSymbolInfo &info)
{
	AdsSymbolEntryAccess *pEntry = Symbol(sym);
	if (pEntry == NULL)
		return false;

	info.nameLength = pEntry->nameLength;
	info.typeLength = pEntry->typeLength;
	info.commentLength = pEntry->commentLength;
	info.entryLength = pEntry->entryLength;
	info.iGrp = pEntry->iGroup;
	info.iOffs = pEntry->iOffs;
	info.size = pEntry->size;
	info.dataType = pEntry->dataType;
	info.flags = pEntry->flags;
	info.name = pEntry->name();
	info.fullname = pEntry->name();
	info.type = pEntry->type();
	info.comment = pEntry->comment();

	return true;
}

bool CAdsParseSymbols::SubSymbolInfo(CAdsSymbolInfo main, uint32_t sub, CAdsSymbolInfo &info)
{
	if (!main.m_pEntry)
		main.m_pEntry = GetTypeByName(main.type);
	auto pEntry = main.m_pEntry;
	if (pEntry)
	{
		if (pEntry->subItemCount)
		{
			auto pSEntry = AdsDatatypeStructItem(pEntry, sub);
			if (pSEntry)
			{
				info.iGrp = main.iGrp;
				info.iOffs = main.iOffs + pSEntry->offs;
				info.size = pSEntry->size;
				info.dataType = pSEntry->dataType;
				info.flags = pSEntry->flags;
				info.name = pSEntry->name();
				info.fullname = string_format("%s.%s", main.fullname, info.name);
				info.type = pSEntry->type();
				info.comment = pSEntry->comment();
				return true;
			}
		}
		else if (pEntry->arrayDim)
		{
			unsigned int x[10] = {0}, baseSize = pEntry->size;
			x[pEntry->arrayDim] = 1;
			auto pAI = pEntry->arrayInfo();
			for (int i = pEntry->arrayDim - 1; i >= 0; i--)
			{
				x[i] = x[i + 1] * pAI[i].elements;
				if (pAI[i].elements)
					baseSize /= pAI[i].elements;
			}
			if (sub < x[0])
			{
				info.iGrp = main.iGrp;
				info.iOffs = main.iOffs;
				info.size = baseSize;
				info.dataType = pEntry->dataType;
				info.flags = pEntry->flags;
				info.type = pEntry->type();
				info.comment = pEntry->comment();
				std::string arr = "[";
				std::string tmp = "";
				for (int i = 0; i < pEntry->arrayDim; i++)
				{
					tmp = string_format("%d", pAI[i].lBound + sub / x[i + 1]);
					arr += tmp;
					arr += (i == pEntry->arrayDim - 1) ? "]" : ",";
					info.iOffs += baseSize * x[i + 1] * (sub / x[i + 1]);
					sub %= x[i + 1];
				}
				info.name = main.name + arr;
				info.fullname = main.fullname + arr;
				return true;
			}
		}
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////
AdsSymbolParser::AdsSymbolParser(long amsClientPort, const AmsAddr &amsAddr)
{
	m_pDynSymbols = NULL;
	m_pCurSubSymbol = NULL;

	m_amsAddr = amsAddr;
	m_amsClientPort = amsClientPort;
}

AdsSymbolParser::~AdsSymbolParser()
{
	if (m_pDynSymbols)
		delete m_pDynSymbols;
	if (m_pCurSubSymbol)
		delete m_pCurSubSymbol;
}

long AdsSymbolParser::load(std::unordered_map<std::string, AdsSymbolEntry> &adsSymbolMap)
{
	auto errorCode = next();
	if (errorCode)
	{
		printf("AdsSymbolParser::next() returned error code: %lu\n", errorCode);
		return errorCode;
	}
	long count = 0;
	// auto it = adsSymbolMap.find(this->fullname);
	while (count < 10000)
	{
		adsSymbolMap.insert(std::make_pair(this->fullname, this->adsSymbolEntry));
		printf("Insert symbol: %s\n", this->fullname.c_str());
		errorCode = child();
		if (!errorCode)
			continue;
		printf("AdsSymbolParser::child() returned error code: %lu\n", errorCode);
		errorCode = parent();
		if (!errorCode)
			continue;
		printf("AdsSymbolParser::parent() returned error code: %lu\n", errorCode);
		errorCode = sibling();
		if (!errorCode)
			continue;
		printf("AdsSymbolParser::sibling() returned error code: %lu\n", errorCode);
		errorCode = next();
		if (!errorCode)
			continue;
		printf("AdsSymbolParser::next() returned error code: %lu\n", errorCode);
		// it = adsSymbolMap.find(this->fullname);
	}
	return errorCode;
}

long AdsSymbolParser::parent()
{
	return AdsGetNextDynSymbol(ADSDYNSYM_GET_PARENT, name, fullname, type, comment, this->adsSymbolEntry.dataType,
							   this->adsSymbolEntry.size, this->adsSymbolEntry.iGroup, this->adsSymbolEntry.iOffs);
}

long AdsSymbolParser::sibling()
{
	return AdsGetNextDynSymbol(ADSDYNSYM_GET_SIBLING, name, fullname, type, comment, this->adsSymbolEntry.dataType,
							   this->adsSymbolEntry.size, this->adsSymbolEntry.iGroup, this->adsSymbolEntry.iOffs);
}

long AdsSymbolParser::child()
{
	return AdsGetNextDynSymbol(ADSDYNSYM_GET_CHILD, name, fullname, type, comment, this->adsSymbolEntry.dataType,
							   this->adsSymbolEntry.size, this->adsSymbolEntry.iGroup, this->adsSymbolEntry.iOffs);
}

long AdsSymbolParser::next()
{
	return AdsGetNextDynSymbol(ADSDYNSYM_GET_NEXT, name, fullname, type, comment, this->adsSymbolEntry.dataType,
							   this->adsSymbolEntry.size, this->adsSymbolEntry.iGroup, this->adsSymbolEntry.iOffs);
}

long AdsSymbolParser::AdsSetFirstDynSymbol(bool bForceReload)
{
	long nResult = ADSERR_NOERR;
	printf("AdsSetFirstDynSymbol(%d)\n", bForceReload);
	if (bForceReload)
	{
		if (m_pDynSymbols != NULL)
			delete m_pDynSymbols;
		m_pDynSymbols = NULL;
	}
	m_nCurDynSymbol = -1;
	m_nNextNavType = ADSDYNSYM_GET_SIBLING;
	while (m_pCurSubSymbol)
	{
		ADSDYNSYM_SUBINFO *tmp = m_pCurSubSymbol->pParent;
		delete m_pCurSubSymbol;
		m_pCurSubSymbol = tmp;
	}
	uint32_t numBytesRead = 0;
	if (m_pDynSymbols != NULL)
	{
		printf("m_pDynSymbols was not null.\n");
		return ADSERR_DEVICE_NOMEMORY;
	}
	AdsSymbolUploadInfo2 info;
	printf("AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_UPLOADINFO2, 0, sizeof(info), &info, &numBytesRead);\n");
	nResult = AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_UPLOADINFO2, 0, sizeof(info), &info, &numBytesRead);
	if (nResult != ADSERR_NOERR)
	{
		printf("Error when trying to get ADSIGRP_SYM_UPLOADINFO2: %lu\n", nResult);
		return nResult;
	}
	printf("info.nSymSize = %u\n", info.nSymSize);
	char *pSym = new char[info.nSymSize];
	if (!pSym)
	{
		printf("Failed to allocate memory for pointer to symbols.\n");
		return ADSERR_DEVICE_NOMEMORY;
	}
	printf("AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_UPLOAD, 0, info.nSymSize, pSym, &numBytesRead);\n");
	nResult = AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_UPLOAD, 0, info.nSymSize, pSym, &numBytesRead);
	if (nResult != ADSERR_NOERR)
	{
		printf("Error when trying to get ADSIGRP_SYM_UPLOAD: %lu\n", nResult);
		return nResult;
	}
	printf("info.nDatatypeSize = %u\n", info.nDatatypeSize);
	char *pDT = new char[info.nDatatypeSize];
	if (!pDT)
	{
		printf("Failed to allocate memory for pointer to datatypes.\n");
		return ADSERR_DEVICE_NOMEMORY;
	}
	printf("AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_DT_UPLOAD, 0, info.nDatatypeSize, pDT, &numBytesRead);\n");
	nResult = AdsSyncReadReqEx2(m_amsClientPort, &m_amsAddr, ADSIGRP_SYM_DT_UPLOAD, 0, info.nDatatypeSize, pDT, &numBytesRead);
	if (nResult != ADSERR_NOERR)
	{
		printf("Error when trying to get ADSIGRP_SYM_DT_UPLOAD: %lu\n", nResult);
		return nResult;
	}
	m_pDynSymbols = new CAdsParseSymbols(pSym, info.nSymSize, pDT, info.nDatatypeSize);
	if (m_pDynSymbols == NULL)
	{
		printf("Error when trying to construct CAdsParseSymbols: %lu\n", nResult);
		return ADSERR_DEVICE_NOMEMORY;
	}
	delete pDT;
	delete pSym;

	return nResult;
}

long AdsSymbolParser::AdsGetNextDynSymbol(uint32_t navType, std::string &strName, std::string &strFullName,
										  std::string &strType, std::string &strComment, uint32_t &adsType,
										  uint32_t &cbSymbolSize, uint32_t &nIndexGroup, uint32_t &nIndexOffset)
{
	long nResult = ADSERR_NOERR;
	long internalNayType = navType;

	if (navType == ADSDYNSYM_GET_NEXT)
		internalNayType = m_nNextNavType;

	if (m_pDynSymbols == NULL)
		nResult = AdsSetFirstDynSymbol(true);

	if (m_pDynSymbols && nResult == ADSERR_NOERR)
	{
		nResult = ADSERR_DEVICE_SYMBOLNOTFOUND;
		CAdsSymbolInfo info;
		switch (internalNayType)
		{
		case ADSDYNSYM_GET_SIBLING:
			if (m_pCurSubSymbol)
			{
				if (m_pDynSymbols->SubSymbolInfo(m_pCurSubSymbol->infoParent, ++m_pCurSubSymbol->nSub, info))
					nResult = ADSERR_NOERR;
			}
			else
			{
				if (m_pDynSymbols->Symbol(++m_nCurDynSymbol, info))
					nResult = ADSERR_NOERR;
			}
			break;
		case ADSDYNSYM_GET_CHILD:
			if (m_pCurSubSymbol)
			{
				CAdsSymbolInfo main;
				if (m_pDynSymbols->SubSymbolInfo(m_pCurSubSymbol->infoParent, m_pCurSubSymbol->nSub, main))
				{
					if (m_pDynSymbols->SubSymbolInfo(main, 0, info))
					{
						ADSDYNSYM_SUBINFO *pTmp = m_pCurSubSymbol;
						m_pCurSubSymbol = new ADSDYNSYM_SUBINFO();
						if (m_pCurSubSymbol)
						{
							m_pCurSubSymbol->infoParent = main;
							m_pCurSubSymbol->nSub = 0;
							m_pCurSubSymbol->pParent = pTmp;
							nResult = ADSERR_NOERR;
						}
						else
						{
							m_pCurSubSymbol = pTmp;
							nResult = ADSERR_DEVICE_NOMEMORY;
						}
					}
				}
			}
			else
			{
				CAdsSymbolInfo main;
				if (m_pDynSymbols->Symbol(m_nCurDynSymbol, main))
				{
					if (m_pDynSymbols->SubSymbolInfo(main, 0, info))
					{
						m_pCurSubSymbol = new ADSDYNSYM_SUBINFO();
						if (m_pCurSubSymbol)
						{
							m_pCurSubSymbol->infoParent = main;
							m_pCurSubSymbol->nSub = 0;
							m_pCurSubSymbol->pParent = NULL;
							nResult = ADSERR_NOERR;
						}
						else
							nResult = ADSERR_DEVICE_NOMEMORY;
					}
				}
			}
			break;
		case ADSDYNSYM_GET_PARENT:
			if (m_pCurSubSymbol)
			{
				ADSDYNSYM_SUBINFO *pTmp = m_pCurSubSymbol;
				m_pCurSubSymbol = m_pCurSubSymbol->pParent;
				delete pTmp;
				if (m_pCurSubSymbol)
				{
					if (m_pDynSymbols->SubSymbolInfo(m_pCurSubSymbol->infoParent, ++m_pCurSubSymbol->nSub, info))
						nResult = ADSERR_NOERR;
				}
				else
				{
					if (m_pDynSymbols->Symbol(++m_nCurDynSymbol, info))
						nResult = ADSERR_NOERR;
				}
			}
			break;
		}
		if (nResult == ADSERR_NOERR)
		{
			strName = info.name;
			strFullName = info.fullname;
			strType = info.type;
			strComment = info.comment;
			adsSymbolEntry.dataType = info.dataType;
			adsSymbolEntry.size = info.size;
			adsSymbolEntry.iGroup = info.iGrp;
			adsSymbolEntry.iOffs = info.iOffs;
			adsSymbolEntry.entryLength = info.entryLength;
			adsSymbolEntry.nameLength = info.nameLength;
			adsSymbolEntry.typeLength = info.typeLength;
			adsSymbolEntry.commentLength = info.commentLength;
			adsSymbolEntry.flags = info.flags;
		}
	}

	if (navType == ADSDYNSYM_GET_NEXT)
	{
		if (nResult != ADSERR_NOERR)
		{
			switch (internalNayType)
			{
			case ADSDYNSYM_GET_CHILD:
				m_nNextNavType = ADSDYNSYM_GET_SIBLING;
				break;
			case ADSDYNSYM_GET_SIBLING:
			case ADSDYNSYM_GET_PARENT:
				m_nNextNavType = ADSDYNSYM_GET_PARENT;
				break;
			}
			if (m_nNextNavType != ADSDYNSYM_GET_PARENT || m_pCurSubSymbol != NULL)
				nResult = AdsGetNextDynSymbol(navType, strName, strFullName, strType, strComment, adsType,
											  cbSymbolSize, nIndexGroup, nIndexOffset);
		}
		else
		{
			switch (internalNayType)
			{
			case ADSDYNSYM_GET_SIBLING:
			case ADSDYNSYM_GET_PARENT:
				m_nNextNavType = ADSDYNSYM_GET_CHILD;
				break;
			}
		}
	}

	return nResult;
}

const AdsSymbolEntryAccess *AdsSymbolEntryAccess::maybeNext() const
{
	return reinterpret_cast<const AdsSymbolEntryAccess *>(reinterpret_cast<const char *>(this) + entryLength);
}