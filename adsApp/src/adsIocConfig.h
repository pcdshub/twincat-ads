#ifndef ADS_IOC_CONFIG_H_
#define ADS_IOC_CONFIG_H_

#include <stdint.h>

#include <string>
#include <vector>

/** Validated settings for the single-target ADS IOC.
 *
 * Defaults here are protocol/application defaults only. A loaded TOML file is
 * applied before adsAsynPortDriverConfigure(), whose non-empty legacy
 * arguments take precedence for fields already present in that interface.
 */
struct AdsIocOptions
{
    AdsIocOptions();

    std::string targetIp;
    std::string targetAmsNetId;
    std::string localIp;
    std::string localAmsNetId;
    uint16_t systemServicePort;
    std::vector<uint16_t> plcRuntimePorts;
    uint16_t ncConfigPort;
    uint16_t ncSymbolPort;
    uint16_t etherCatMasterPort;
    uint16_t loggerPort;
    uint16_t adsTcpPort;
    uint16_t adsDiscoveryUdpPort;

    bool enableNcParameterWrites;
    bool enableCoeParameterWrites;
    bool enableEcStateRequests;
    bool enableAdsStateRequests;
    bool enableIcmpPingCheck;
    bool continueIfPingFails;
    bool enableAdsReachabilityCheck;
    bool enableAutoRouteDiagnostics;
    bool enableRouteCreationCommands;
    bool enableEventLoggerConnection;
    bool allowIndexOffsetFallback;
    bool allowSlowRecoveryRetry;

    double heartbeatPeriodSec;
    unsigned int heartbeatFailureThreshold;
    double routeCheckInitialBackoffSec;
    double routeCheckMaxBackoffSec;
    double bulkReadRetryWindowSec;
    double bulkWriteGroupWindowSec;
    double slowRecoveryRetryPeriodSec;
    unsigned int adsRequestTimeoutMs;

    unsigned int maxBulkReadBatchSize;
    unsigned int maxBulkWriteBatchSize;
    unsigned int maxFailedSymbolsPerCycle;
    unsigned int maxEventLoggerMessagesPerSec;
    unsigned int maxDiagnosticLogRate;
};

/** Parse and validate TOML text into options. The targetIp key is required. */
bool parseAdsIocConfig(const std::string& tomlText, AdsIocOptions& options, std::string& error);

/** Parse and install the single-target TOML settings for subsequent configure calls. */
bool loadAdsIocConfig(const std::string& path, std::string& error);

/** Get a thread-safe copy of the currently loaded options. */
AdsIocOptions getAdsIocOptions();

/** Replace the process-wide single-target options used by the configure call. */
void setAdsIocOptions(const AdsIocOptions& options);

/** True after a successful loadAdsIocConfig() call. */
bool adsIocConfigIsLoaded();

#endif // ADS_IOC_CONFIG_H_
