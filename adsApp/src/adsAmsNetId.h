#ifndef ADS_AMS_NET_ID_H_
#define ADS_AMS_NET_ID_H_

#include "AdsLib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace ads
{
inline bool parseAmsNetId(const char* text, AmsNetId* netId)
{
    if (!text || !netId || !*text)
        return false;

    unsigned int bytes[6] = {};
    char trailingCharacter = '\0';
    const int parsed = std::sscanf(text,
                                   "%u.%u.%u.%u.%u.%u%c",
                                   &bytes[0],
                                   &bytes[1],
                                   &bytes[2],
                                   &bytes[3],
                                   &bytes[4],
                                   &bytes[5],
                                   &trailingCharacter);
    if (parsed != 6 ||
        !std::all_of(bytes, bytes + 6, [](unsigned int byte) {
            return byte <= std::numeric_limits<std::uint8_t>::max();
        }))
    {
        return false;
    }

    *netId = AmsNetId(static_cast<uint8_t>(bytes[0]),
                      static_cast<uint8_t>(bytes[1]),
                      static_cast<uint8_t>(bytes[2]),
                      static_cast<uint8_t>(bytes[3]),
                      static_cast<uint8_t>(bytes[4]),
                      static_cast<uint8_t>(bytes[5]));
    return true;
}
} // namespace ads

#endif // ADS_AMS_NET_ID_H_
