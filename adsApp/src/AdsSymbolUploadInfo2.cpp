#include "AdsSymbolUploadInfo2.h"
#include <AdsLib.h>
#include <stdexcept>

AdsSymbolUploadInfo2 AdsSymbolUploadInfo2::fromAmsAddr(long adsClientPort, const AmsAddr &amsAddr, long& errorCode)
{
    AdsSymbolUploadInfo2 info;
    errorCode = AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_UPLOADINFO2, 0, sizeof(info), &info, nullptr);
    printf("ADSIGRP_SYM_UPLOADINFO2 result:\n");
    printf("info.nSymbols: %u\n", info.nSymbols);
    printf("info.nSymSize: %u\n", info.nSymSize);
    printf("info.nDatatypes: %u\n", info.nDatatypes);
    printf("info.nDatatypeSize: %u\n", info.nDatatypeSize);
    printf("info.nMaxDynSymbols: %u\n", info.nMaxDynSymbols);
    printf("info.nUsedDynSymbols: %u\n", info.nUsedDynSymbols);
    return info;
}

long AdsSymbolUploadInfo2::uploadSymbols(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &symbols) const
{
    uint32_t bytesRead = 0;
    symbols.resize(nSymSize);
    return AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_UPLOAD, 0, nSymSize, symbols.data(), &bytesRead);
}

long AdsSymbolUploadInfo2::uploadDatatypes(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &datatypes) const
{
    uint32_t bytesRead = 0;
    datatypes.resize(nDatatypeSize);
    return AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_DT_UPLOAD, 0, nDatatypeSize, datatypes.data(), &bytesRead);
}