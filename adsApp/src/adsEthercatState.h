#ifndef ADS_ETHERCAT_STATE_H_
#define ADS_ETHERCAT_STATE_H_

#include "adsAsynPortDriverUtils.h"

#include <cstdint>

enum class AdsEthercatDeviceState : int32_t
{
    Init      = 0x01,
    PreOp     = 0x02,
    Bootstrap = 0x03,
    SafeOp    = 0x04,
    Op        = 0x08,
};

constexpr uint32_t ADSIGRP_ECAT_SLAVE_STATE = 0x00000009;
constexpr uint32_t ADS_ECAT_STATE_SIZE_BYTES = sizeof(uint16_t);
constexpr uint32_t ADS_ECAT_UINT16_TYPE = ADST_UINT16;
constexpr int32_t ADS_ECAT_UNDEFINED_STATE_REQUEST = 0;
constexpr uint16_t ADS_ECAT_R0_IO_AMS_PORT = 300;
constexpr uint32_t ADS_ECAT_IO_DEVICE_STATE_INDEX_GROUP_BASE = 0x5000;
constexpr uint32_t ADS_ECAT_IO_READ_DEVICE_IDS_OFFSET = 0x01;
constexpr uint32_t ADS_ECAT_IO_READ_DEVICE_COUNT_OFFSET = 0x02;
constexpr uint32_t ADS_ECAT_IO_READ_DEVICE_NET_ID_OFFSET = 0x05;
constexpr uint32_t ADS_ECAT_IO_DEVICE_ID_TABLE_FIRST_ID_INDEX = 1;
constexpr uint32_t ADS_ECAT_IO_DEVICE_ID_TABLE_WORDS_FOR_SINGLE_DEVICE = 2;
constexpr uint32_t ADS_ECAT_EXPECTED_SINGLE_IO_DEVICE_COUNT = 1;
constexpr uint32_t ADS_ECAT_AMS_NET_ID_SIZE_BYTES = 6;
constexpr uint32_t ADS_ECAT_AMS_NET_ID_IP_PREFIX_BYTES = 4;
constexpr uint8_t ADS_ECAT_DERIVED_AMS_PORT_HIGH_BYTE = 2;
constexpr uint8_t ADS_ECAT_DERIVED_AMS_PORT_LOW_BYTE = 1;
constexpr uint32_t ADS_ECAT_STATE_SCAN_SECONDS_DEFAULT = 2;

inline bool adsIsValidEthercatStateRequest(uint32_t group,
                                           uint32_t size,
                                           uint32_t dataType,
                                           int32_t value)
{
    if (group != ADSIGRP_ECAT_SLAVE_STATE)
    {
        return true;
    }

    if (size != ADS_ECAT_STATE_SIZE_BYTES || dataType != ADS_ECAT_UINT16_TYPE)
    {
        return false;
    }

    switch (value)
    {
    case static_cast<int32_t>(AdsEthercatDeviceState::Init):
    case static_cast<int32_t>(AdsEthercatDeviceState::PreOp):
    case static_cast<int32_t>(AdsEthercatDeviceState::Bootstrap):
    case static_cast<int32_t>(AdsEthercatDeviceState::SafeOp):
    case static_cast<int32_t>(AdsEthercatDeviceState::Op):
        return true;
    default:
        return false;
    }
}

#endif
