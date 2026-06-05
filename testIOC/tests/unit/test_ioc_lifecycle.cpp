/**
 * test_ioc_lifecycle.cpp
 *
 * Google Test suite for adsAsynPortDriver following the IOC boot sequence.
 * Requires pyads test server running on 127.0.0.1:48898.
 *
 * Stages:
 *   1 — Connect to ADS server
 *   2 — resolveSymbolInfo  (batch SYM_INFOBYNAMEEX)
 *   3 — resolveSymbolHandles (batch SYM_HNDBYNAME)
 *   5 — bulkOK timing (PINI fix validation)
 *   6 — Bulk read thread (SUMUP_READ polling)
 *   7 — Notification callbacks
 *   8 — Runtime read/write
 *   9 — Disconnect / cleanup
 *
 * Prerequisites:
 *   - pyads test server running on 127.0.0.1:48898
 *   - testIOC/DB/TestIOC.db present
 *   - Built with -DADS_UNIT_TEST
 *
 * Build:
 *   make -C adsApp USR_CXXFLAGS+="-DADS_UNIT_TEST -DCONFIG_DEFAULT_LOGLEVEL=1"
 *   make -C testIOC/tests/unit
 *
 * Run:
 *   make -C testIOC/tests/unit runtests
 */

#include <gtest/gtest.h>
#include <atomic>
#include <string>

#include "adsAsynPortDriver.h"
#include "epicsThread.h"
#include "dbAccess.h"
#include "iocInit.h"
#include "dbStaticLib.h"

// ── Connection parameters ─────────────────────────────────────────────────────
static constexpr const char  *TEST_IP       = "127.0.0.1";
static constexpr const char  *TEST_AMSID    = "127.0.0.1.1.1";
static constexpr unsigned int TEST_AMS_PORT = 851;
static constexpr const char  *TEST_PORT     = "ADS_LIFECYCLE";
static constexpr int TEST_PARAM_TABLE_SIZE = 30000;
// ── Well-known test symbols ───────────────────────────────────────────────────
static constexpr const char *SYM_BOOL  = "GVL_Logger.bTrickleTripped";
static constexpr const char *SYM_DINT  = "GVL_Logger.iLogPort";
static constexpr const char *SYM_LREAL = "Main.M1.fBacklash";

// ── Test instrumentation (always defined — built with ADS_UNIT_TEST) ──────────
extern std::atomic<int> g_callbackCount;
extern "C" int testIoc_registerRecordDeviceDriver(struct dbBase *pbase);
extern int initHook(void);

// ─────────────────────────────────────────────────────────────────────────────
// Shared fixture — one driver instance for all lifecycle stages
// ─────────────────────────────────────────────────────────────────────────────
class AdsLifecycleTest : public ::testing::Test
{
protected:
    static adsAsynPortDriver *driver;
    static asynUser          *pasynUser;

    static void SetUpTestSuite()
    {
        // 1. Load DBD and register device drivers
        dbLoadDatabase(TEST_DBD_PATH, nullptr, nullptr);
        testIoc_registerRecordDeviceDriver(pdbbase);

        // 2. Load DB
        dbLoadRecords(TEST_DB_PATH, "PREFIX=TEST:,PORT=ADS_LIFECYCLE");

        // 3. Create driver — must be registered before iocInit
        driver = new adsAsynPortDriver(
            TEST_PORT,
            TEST_IP,
            TEST_AMSID,
            TEST_AMS_PORT,
            TEST_PARAM_TABLE_SIZE,               // paramTableSize
            50,                 // priority
            1,                  // autoConnect
            500,                // defaultSampleTimeMS
            600,                // maxDelayTimeMS
            1000,               // adsTimeoutMS
            ADS_TIME_BASE_EPICS
        );
        ASSERT_NE(driver, nullptr);

        // 4. Register initHook and set global instance
        //    (normally done by adsAsynPortDriverConfigure)
        initHook();
        adsAsynPortDriver::setGlobalInstance(driver);

        pasynUser = pasynManager->createAsynUser(nullptr, nullptr);
        ASSERT_NE(pasynUser, nullptr);
        pasynManager->connectDevice(pasynUser, TEST_PORT, 0);

        // 5. iocInit — triggers full boot sequence:
        //    drvUserCreate → buildDrvInfoCache → resolveSymbolInfo
        //    → resolveSymbolHandles → bulkOK=1
        iocInit();
		const char *syms[] = {"Main.M1.bUserBacklashEn", "Main.M1.bUserEnable", "Main.M1.fBacklash"};
		for (auto sym : syms) {
			const AdsSymbolDictEntry *e = driver->lookupSymbol(sym);
			if (e) printf("%s: size=%u adst=%u\n", sym, e->size, e->adst);
		}
        // 6. Wait for bulk thread to start and callbacks to settle
        epicsThreadSleep(3.0);

        printf("bulkOK after iocInit = %d\n", driver->getBulkOK());
        const AdsSymbolDictEntry *e = driver->lookupSymbol(SYM_DINT);
        if (e) printf("%s: size=%u adst=%u resolved=%d handle=%u\n",
                      SYM_DINT, e->size, e->adst, e->resolved, e->handle);
    }

    static void TearDownTestSuite()
    {
        if (pasynUser)
        {
            pasynManager->freeAsynUser(pasynUser);
            pasynUser = nullptr;
        }
        //delete driver;
        //driver = nullptr;
    }
};

adsAsynPortDriver *AdsLifecycleTest::driver    = nullptr;
asynUser          *AdsLifecycleTest::pasynUser = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Stage 1 — Connection
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage1_ConnectedToServer)
{
    EXPECT_TRUE(driver->isConnected())
        << "Driver should be connected to pyads test server";
}

TEST_F(AdsLifecycleTest, Stage1_AdsPortOpen)
{
    EXPECT_GT(driver->getAdsPort(), 0)
        << "ADS local port should be open";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 2 — resolveSymbolInfo
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage2_SymbolDictPopulated)
{
    EXPECT_GT(driver->symbolDictSize(), 0u)
        << "symbolDict_ should be populated after resolveSymbolInfo";
}

TEST_F(AdsLifecycleTest, Stage2_LrealSymbolResolved)
{
    const AdsSymbolDictEntry *e = driver->lookupSymbol(SYM_LREAL);
    ASSERT_NE(e, nullptr) << "LREAL symbol not found: " << SYM_LREAL;
    EXPECT_EQ(e->size, 8u);
    EXPECT_EQ(e->adst, (uint32_t)ADST_REAL64);
}

TEST_F(AdsLifecycleTest, Stage2_BoolSymbolResolved)
{
    const AdsSymbolDictEntry *e = driver->lookupSymbol(SYM_BOOL);
    ASSERT_NE(e, nullptr) << "BOOL symbol not found: " << SYM_BOOL;
    EXPECT_EQ(e->size, 1u);
    EXPECT_EQ(e->adst, (uint32_t)ADST_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 3 — resolveSymbolHandles
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage3_HandlesAcquired)
{
    const AdsSymbolDictEntry *e = driver->lookupSymbol(SYM_LREAL);
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->resolved) << "Symbol should have a resolved handle";
    EXPECT_GT(e->handle, 0u) << "Handle should be non-zero";
}

TEST_F(AdsLifecycleTest, Stage3_AllTestSymbolsHaveHandles)
{
    const char *syms[] = { SYM_BOOL, SYM_DINT, SYM_LREAL };
    for (const char *sym : syms)
    {
        const AdsSymbolDictEntry *e = driver->lookupSymbol(sym);
        ASSERT_NE(e, nullptr) << "Symbol not found: " << sym;
        EXPECT_TRUE(e->resolved) << "No handle for: " << sym;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 5 — bulkOK timing (PINI fix)
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage5_BulkOKSet)
{
    EXPECT_EQ(driver->getBulkOK(), 1)
        << "bulkOK should be 1 after IOC is running";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 6 — Bulk read thread
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage6_DriverRemainsConnectedAfterPolling)
{
    epicsThreadSleep(1.5);
    EXPECT_TRUE(driver->isConnected())
        << "Driver should remain connected after bulk read cycles";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 7 — Notification callbacks
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage7_CallbacksFired)
{
    int before = g_callbackCount.load();
    epicsThreadSleep(0.5);
    int after = g_callbackCount.load();
    EXPECT_GT(after, before)
        << "At least one data callback should have fired "
        << "(before=" << before << " after=" << after << ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 8 — Runtime read
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage8_DirectReadSucceeds)
{
    int index = 0;
    asynStatus status = driver->findParam(SYM_LREAL, &index);
    if (status != asynSuccess)
    {
        GTEST_SKIP() << "Symbol not registered as param";
    }

    pasynUser->reason = index;
    epicsFloat64 val  = 0.0;
    status = driver->readFloat64(pasynUser, &val);
    EXPECT_EQ(status, asynSuccess) << "readFloat64 should succeed";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 9 — Disconnect preserves route
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(AdsLifecycleTest, Stage9_DisconnectPreservesRoute)
{
    driver->disconnect(pasynUser);
    epicsThreadSleep(0.1);
    EXPECT_TRUE(driver->isRouteAdded())
        << "ADS route should be preserved after disconnect";
}

TEST_F(AdsLifecycleTest, Stage9_ReconnectSucceeds)
{
    asynStatus status = driver->connect(pasynUser);
    epicsThreadSleep(1.0);
    EXPECT_EQ(status, asynSuccess) << "Reconnect should succeed";
    EXPECT_TRUE(driver->isConnected())
        << "Driver should be connected after reconnect";
}
