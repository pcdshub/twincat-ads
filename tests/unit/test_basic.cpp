/**
 * test_basic.cpp
 *
 * Basic smoke test for adsAsynPortDriver.
 * Verifies the driver can be instantiated and connects
 * to the pyads test server running on 127.0.0.1:48898.
 */

#include <gtest/gtest.h>
#include "adsAsynPortDriver.h"
#include "adsAmsNetId.h"
#include "adsEthercatState.h"
#include "epicsThread.h"

static constexpr const char *TEST_IP    = "127.0.0.1";
static constexpr const char *TEST_AMSID = "127.0.0.1.1.1";
static constexpr unsigned int TEST_PORT = 851;
static constexpr const char *TEST_ECAT_MASTER_AMSID_OVERRIDE = "127.0.0.1.3.1";

TEST(AdsAmsNetId, ParsesValidAddress)
{
    AmsNetId netId;
    ASSERT_TRUE(ads::parseAmsNetId("127.0.0.1.3.1", &netId));

    EXPECT_EQ(netId.b[0], 127);
    EXPECT_EQ(netId.b[1], 0);
    EXPECT_EQ(netId.b[2], 0);
    EXPECT_EQ(netId.b[3], 1);
    EXPECT_EQ(netId.b[4], 3);
    EXPECT_EQ(netId.b[5], 1);
}

TEST(AdsAmsNetId, RejectsMalformedAddress)
{
    AmsNetId netId;
    EXPECT_FALSE(ads::parseAmsNetId(nullptr, &netId));
    EXPECT_FALSE(ads::parseAmsNetId("", &netId));
    EXPECT_FALSE(ads::parseAmsNetId("127.0.0.1.3", &netId));
    EXPECT_FALSE(ads::parseAmsNetId("127.0.0.1.256.1", &netId));
    EXPECT_FALSE(ads::parseAmsNetId("127.0.0.1.3.1x", &netId));
}

TEST(AdsDriverBasic, DriverInstantiates)
{
    adsAsynPortDriver *driver = new adsAsynPortDriver(
        "ADS_TEST",
        TEST_IP,
        TEST_AMSID,
        TEST_PORT,
        1000,               // paramTableSize
        50,                 // priority
        1,                  // autoConnect
        100,                // defaultSampleTimeMS
        150,                // maxDelayTimeMS
        1000,               // adsTimeoutMS
        ADS_TIME_BASE_EPICS // defaultTimeSource
    );

    ASSERT_NE(driver, nullptr);
    epicsThreadSleep(2.0);

#ifdef ADS_UNIT_TEST
    EXPECT_TRUE(driver->isConnected())
        << "Driver should be connected to pyads test server";
    const AmsNetId& discoveredMasterId = driver->getEthercatMasterAmsNetId();
    EXPECT_EQ(discoveredMasterId.b[0], 127);
    EXPECT_EQ(discoveredMasterId.b[1], 0);
    EXPECT_EQ(discoveredMasterId.b[2], 0);
    EXPECT_EQ(discoveredMasterId.b[3], 1);
    EXPECT_EQ(discoveredMasterId.b[4], ADS_ECAT_DERIVED_AMS_PORT_HIGH_BYTE);
    EXPECT_EQ(discoveredMasterId.b[5], ADS_ECAT_DERIVED_AMS_PORT_LOW_BYTE);
#endif

    delete driver;
}

TEST(AdsEthercatState, UsesConfiguredMasterAmsNetIdOverride)
{
    adsAsynPortDriver* driver = new adsAsynPortDriver(
        "ADS_ECAT_OVERRIDE_TEST",
        TEST_IP,
        TEST_AMSID,
        TEST_PORT,
        1000,
        50,
        1,
        100,
        150,
        1000,
        ADS_TIME_BASE_EPICS,
        TEST_ECAT_MASTER_AMSID_OVERRIDE);

    ASSERT_NE(driver, nullptr);
    const AmsNetId& masterId = driver->getEthercatMasterAmsNetId();
    EXPECT_EQ(masterId.b[0], 127);
    EXPECT_EQ(masterId.b[1], 0);
    EXPECT_EQ(masterId.b[2], 0);
    EXPECT_EQ(masterId.b[3], 1);
    EXPECT_EQ(masterId.b[4], 3);
    EXPECT_EQ(masterId.b[5], 1);
    delete driver;
}

TEST(AdsEthercatState, AcceptsSupportedStateRequests)
{
    for (const AdsEthercatDeviceState state : {
             AdsEthercatDeviceState::Init,
             AdsEthercatDeviceState::PreOp,
             AdsEthercatDeviceState::Bootstrap,
             AdsEthercatDeviceState::SafeOp,
             AdsEthercatDeviceState::Op,
         })
    {
        EXPECT_TRUE(adsIsValidEthercatStateRequest(
            ADSIGRP_ECAT_SLAVE_STATE,
            ADS_ECAT_STATE_SIZE_BYTES,
            ADS_ECAT_UINT16_TYPE,
            static_cast<int32_t>(state)));
    }
}

TEST(AdsEthercatState, RejectsUnsupportedStateRequestsAndTypes)
{
    constexpr int32_t unsupportedState =
        static_cast<int32_t>(AdsEthercatDeviceState::Op) + 1;
    EXPECT_FALSE(adsIsValidEthercatStateRequest(
        ADSIGRP_ECAT_SLAVE_STATE,
        ADS_ECAT_STATE_SIZE_BYTES,
        ADS_ECAT_UINT16_TYPE,
        unsupportedState));
    EXPECT_FALSE(adsIsValidEthercatStateRequest(
        ADSIGRP_ECAT_SLAVE_STATE,
        ADS_ECAT_STATE_SIZE_BYTES - 1,
        ADS_ECAT_UINT16_TYPE,
        static_cast<int32_t>(AdsEthercatDeviceState::Init)));
    EXPECT_FALSE(adsIsValidEthercatStateRequest(
        ADSIGRP_ECAT_SLAVE_STATE,
        ADS_ECAT_STATE_SIZE_BYTES,
        ADST_UINT32,
        static_cast<int32_t>(AdsEthercatDeviceState::Init)));
    EXPECT_FALSE(adsIsValidEthercatStateRequest(
        ADSIGRP_ECAT_SLAVE_STATE,
        ADS_ECAT_STATE_SIZE_BYTES,
        ADS_ECAT_UINT16_TYPE,
        ADS_ECAT_UNDEFINED_STATE_REQUEST));
    EXPECT_TRUE(adsIsValidEthercatStateRequest(
        ADSIGRP_SYM_VALBYHND, 4, ADST_UINT32, 123));
}
