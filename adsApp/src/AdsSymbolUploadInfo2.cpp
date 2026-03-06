#include "AdsSymbolUploadInfo2.h"
#include <AdsLib.h>
#include <stdexcept>

AdsSymbolUploadInfo2 AdsSymbolUploadInfo2::fromAmsAddr(long adsClientPort, const AmsAddr &amsAddr)
{
    AdsSymbolUploadInfo2 info;
    auto error = AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_UPLOADINFO2, 0, sizeof(info), &info, nullptr);
    if (error)
    {
        throw std::runtime_error("Failed to read ADSIGRP_SYM_UPLOADINFO2.\n");
    }
    return info;
}

long AdsSymbolUploadInfo2::uploadSymbols(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &symbols) const
{
    uint32_t bytesRead = 0;
    return AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_UPLOAD, 0, nSymSize, symbols.data(), &bytesRead);
}

long AdsSymbolUploadInfo2::uploadDatatypes(long adsClientPort, const AmsAddr &amsAddr, std::vector<char> &datatypes) const
{
    uint32_t bytesRead = 0;
    return AdsSyncReadReqEx2(adsClientPort, &amsAddr, ADSIGRP_SYM_DT_UPLOAD, 0, nDatatypeSize, datatypes.data(), &bytesRead);
}
