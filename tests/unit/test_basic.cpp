/**
 * test_basic.cpp
 *
 * Basic smoke test for adsAsynPortDriver.
 * Verifies the driver can be instantiated and connects
 * to the pyads test server running on 127.0.0.1:48898.
 */

#include <gtest/gtest.h>
#include "adsAsynPortDriver.h"
#include "epicsThread.h"

static constexpr const char *TEST_IP    = "127.0.0.1";
static constexpr const char *TEST_AMSID = "127.0.0.1.1.1";
static constexpr unsigned int TEST_PORT = 851;

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
#endif

    delete driver;
}
