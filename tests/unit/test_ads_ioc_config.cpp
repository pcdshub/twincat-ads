#include <gtest/gtest.h>

#include "adsIocConfig.h"

TEST(AdsIocConfig, AppliesDefaultsAndConfiguredValues)
{
    AdsIocOptions options;
    std::string error;
    const std::string config = R"(
[target]
ip = "172.21.148.178"
amsNetId = "172.21.148.178.1.1"
[ports]
plcRuntimePorts = [851, 852]
[behavior]
enableNcParameterWrites = true
["timing"]
heartbeatPeriodSec = 2.5
adsRequestTimeoutMs = 2500
[limits]
maxBulkReadBatchSize = 200
)";

    ASSERT_TRUE(parseAdsIocConfig(config, options, error)) << error;
    EXPECT_EQ(options.targetIp, "172.21.148.178");
    EXPECT_EQ(options.targetAmsNetId, "172.21.148.178.1.1");
    ASSERT_EQ(options.plcRuntimePorts.size(), 2u);
    EXPECT_EQ(options.plcRuntimePorts[0], 851);
    EXPECT_EQ(options.plcRuntimePorts[1], 852);
    EXPECT_TRUE(options.enableNcParameterWrites);
    EXPECT_FALSE(options.enableCoeParameterWrites);
    EXPECT_EQ(options.heartbeatPeriodSec, 2.5);
    EXPECT_EQ(options.adsRequestTimeoutMs, 2500u);
    EXPECT_EQ(options.maxBulkReadBatchSize, 200u);
    EXPECT_EQ(options.systemServicePort, 10000);
    EXPECT_EQ(options.adsTcpPort, 48898);
}

TEST(AdsIocConfig, DefaultsWriteGatesToDisabled)
{
    AdsIocOptions options;
    std::string error;
    ASSERT_TRUE(parseAdsIocConfig("[target]\nip = \"127.0.0.1\"\n", options, error)) << error;
    EXPECT_FALSE(options.enableNcParameterWrites);
    EXPECT_FALSE(options.enableCoeParameterWrites);
    EXPECT_FALSE(options.enableEcStateRequests);
    EXPECT_FALSE(options.enableAdsStateRequests);
}

TEST(AdsIocConfig, RequiresTargetIp)
{
    AdsIocOptions options;
    std::string error;
    EXPECT_FALSE(parseAdsIocConfig("[ports]\nsystemServicePort = 10000\n", options, error));
    EXPECT_NE(error.find("target.ip"), std::string::npos);
}

TEST(AdsIocConfig, RejectsUnknownFields)
{
    AdsIocOptions options;
    std::string error;
    const std::string config = "[target]\nip = \"127.0.0.1\"\nunknown = 7\n";
    EXPECT_FALSE(parseAdsIocConfig(config, options, error));
    EXPECT_NE(error.find("target.unknown"), std::string::npos);
}

TEST(AdsIocConfig, RejectsInvalidPortsAndBackoff)
{
    AdsIocOptions options;
    std::string error;
    EXPECT_FALSE(parseAdsIocConfig(
        "[target]\nip = \"127.0.0.1\"\n[ports]\nsystemServicePort = 70000\n",
        options,
        error));

    error.clear();
    EXPECT_FALSE(parseAdsIocConfig(
        "[target]\nip = \"127.0.0.1\"\n[timing]\nrouteCheckInitialBackoffSec = 10.0\n"
        "routeCheckMaxBackoffSec = 5.0\n",
        options,
        error));
    EXPECT_NE(error.find("routeCheckMaxBackoffSec"), std::string::npos);
}

TEST(AdsIocConfig, RejectsEmptyOrDuplicateRuntimePorts)
{
    AdsIocOptions options;
    std::string error;
    EXPECT_FALSE(parseAdsIocConfig(
        "[target]\nip = \"127.0.0.1\"\n[ports]\nplcRuntimePorts = []\n", options, error));

    error.clear();
    EXPECT_FALSE(parseAdsIocConfig(
        "[target]\nip = \"127.0.0.1\"\n[ports]\nplcRuntimePorts = [851, 851]\n",
        options,
        error));
    EXPECT_NE(error.find("duplicates"), std::string::npos);
}
