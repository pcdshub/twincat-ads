#include "adsIocConfig.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>

#include "toml.hpp"

namespace
{
std::mutex optionsMutex;
AdsIocOptions loadedOptions;
bool hasLoadedOptions = false;

template <typename T>
bool readOptional(const toml::value& table,
                  const std::string& key,
                  T& destination,
                  std::string& error)
{
    if (!table.contains(key))
    {
        return true;
    }
    try
    {
        destination = toml::find<T>(table, key);
    }
    catch (const std::exception& exception)
    {
        error = "invalid value for key '" + key + "': " + exception.what();
        return false;
    }
    return true;
}

bool readSection(const toml::value& root,
                 const std::string& name,
                 const std::set<std::string>& allowedKeys,
                 toml::value& section,
                 std::string& error)
{
    if (!root.contains(name))
    {
        section = toml::value(toml::table{});
        return true;
    }
    section = root.at(name);
    if (!section.is_table())
    {
        error = "section '" + name + "' must be a TOML table";
        return false;
    }
    for (const auto& item : section.as_table())
    {
        if (allowedKeys.count(item.first) == 0)
        {
            error = "unknown key '" + name + "." + item.first + "'";
            return false;
        }
    }
    return true;
}

bool readPort(const toml::value& table,
              const std::string& key,
              uint16_t& destination,
              std::string& error)
{
    int64_t value = destination;
    if (!readOptional(table, key, value, error))
    {
        return false;
    }
    if (value < 0 || value > std::numeric_limits<uint16_t>::max())
    {
        error = "port '" + key + "' must be in range 0..65535";
        return false;
    }
    destination = static_cast<uint16_t>(value);
    return true;
}

bool readPositiveUnsigned(const toml::value& table,
                          const std::string& key,
                          unsigned int& destination,
                          std::string& error)
{
    int64_t value = destination;
    if (!readOptional(table, key, value, error))
    {
        return false;
    }
    if (value < 1 || value > std::numeric_limits<unsigned int>::max())
    {
        error = "'" + key + "' must be a positive integer";
        return false;
    }
    destination = static_cast<unsigned int>(value);
    return true;
}

bool readPositiveDouble(const toml::value& table,
                        const std::string& key,
                        double& destination,
                        std::string& error)
{
    if (!readOptional(table, key, destination, error))
    {
        return false;
    }
    if (!std::isfinite(destination) || destination <= 0.0)
    {
        error = "'" + key + "' must be a finite positive number";
        return false;
    }
    return true;
}

bool readRuntimePorts(const toml::value& table,
                      std::vector<uint16_t>& ports,
                      std::string& error)
{
    if (!table.contains("plcRuntimePorts"))
    {
        return true;
    }
    std::vector<int64_t> configured;
    if (!readOptional(table, "plcRuntimePorts", configured, error))
    {
        return false;
    }
    if (configured.empty())
    {
        error = "'plcRuntimePorts' must contain at least one port";
        return false;
    }

    std::vector<uint16_t> validated;
    std::set<uint16_t> seen;
    for (const int64_t port : configured)
    {
        if (port < 1 || port > std::numeric_limits<uint16_t>::max())
        {
            error = "each 'plcRuntimePorts' value must be in range 1..65535";
            return false;
        }
        const uint16_t converted = static_cast<uint16_t>(port);
        if (!seen.insert(converted).second)
        {
            error = "'plcRuntimePorts' must not contain duplicates";
            return false;
        }
        validated.push_back(converted);
    }
    ports.swap(validated);
    return true;
}

bool validateRootKeys(const toml::value& root, std::string& error)
{
    const std::set<std::string> allowed = {"target", "ports", "behavior", "timing", "limits"};
    for (const auto& item : root.as_table())
    {
        if (allowed.count(item.first) == 0)
        {
            error = "unknown top-level section '" + item.first + "'";
            return false;
        }
    }
    return true;
}
} // namespace

AdsIocOptions::AdsIocOptions()
    : systemServicePort(10000),
      plcRuntimePorts(1, 851),
      ncConfigPort(500),
      ncSymbolPort(501),
      etherCatMasterPort(0xFFFF),
      loggerPort(100),
      adsTcpPort(48898),
      adsDiscoveryUdpPort(48899),
      enableNcParameterWrites(false),
      enableCoeParameterWrites(false),
      enableEcStateRequests(false),
      enableAdsStateRequests(false),
      enableIcmpPingCheck(true),
      continueIfPingFails(false),
      enableAdsReachabilityCheck(true),
      enableAutoRouteDiagnostics(true),
      enableRouteCreationCommands(true),
      enableEventLoggerConnection(true),
      allowIndexOffsetFallback(true),
      allowSlowRecoveryRetry(true),
      heartbeatPeriodSec(5.0),
      heartbeatFailureThreshold(2),
      routeCheckInitialBackoffSec(1.0),
      routeCheckMaxBackoffSec(64.0),
      bulkReadRetryWindowSec(0.1),
      bulkWriteGroupWindowSec(0.1),
      slowRecoveryRetryPeriodSec(30.0),
      adsRequestTimeoutMs(5000),
      maxBulkReadBatchSize(500),
      maxBulkWriteBatchSize(500),
      maxFailedSymbolsPerCycle(500),
      maxEventLoggerMessagesPerSec(100),
      maxDiagnosticLogRate(100)
{
}

bool parseAdsIocConfig(const std::string& tomlText, AdsIocOptions& options, std::string& error)
{
    error.clear();
    try
    {
        const toml::value root = toml::parse_str(tomlText);
        if (!root.is_table())
        {
            error = "configuration root must be a TOML table";
            return false;
        }
        if (!validateRootKeys(root, error))
        {
            return false;
        }

        toml::value target;
        toml::value ports;
        toml::value behavior;
        toml::value timing;
        toml::value limits;
        if (!readSection(root, "target", {"ip", "amsNetId", "localIp", "localAmsNetId"}, target, error) ||
            !readSection(root,
                         "ports",
                         {"systemServicePort",
                          "plcRuntimePorts",
                          "ncConfigPort",
                          "ncSymbolPort",
                          "etherCatMasterPort",
                          "loggerPort",
                          "adsTcpPort",
                          "adsDiscoveryUdpPort"},
                         ports,
                         error) ||
            !readSection(root,
                         "behavior",
                         {"enableNcParameterWrites",
                          "enableCoeParameterWrites",
                          "enableEcStateRequests",
                          "enableAdsStateRequests",
                          "enableIcmpPingCheck",
                          "continueIfPingFails",
                          "enableAdsReachabilityCheck",
                          "enableAutoRouteDiagnostics",
                          "enableRouteCreationCommands",
                          "enableEventLoggerConnection",
                          "allowIndexOffsetFallback",
                          "allowSlowRecoveryRetry"},
                         behavior,
                         error) ||
            !readSection(root,
                         "timing",
                         {"heartbeatPeriodSec",
                          "heartbeatFailureThreshold",
                          "routeCheckInitialBackoffSec",
                          "routeCheckMaxBackoffSec",
                          "bulkReadRetryWindowSec",
                          "bulkWriteGroupWindowSec",
                          "slowRecoveryRetryPeriodSec",
                          "adsRequestTimeoutMs"},
                         timing,
                         error) ||
            !readSection(root,
                         "limits",
                         {"maxBulkReadBatchSize",
                          "maxBulkWriteBatchSize",
                          "maxFailedSymbolsPerCycle",
                          "maxEventLoggerMessagesPerSec",
                          "maxDiagnosticLogRate"},
                         limits,
                         error))
        {
            return false;
        }

        AdsIocOptions parsed;
        if (!target.contains("ip"))
        {
            error = "required setting 'target.ip' is missing";
            return false;
        }
        if (!readOptional(target, "ip", parsed.targetIp, error) ||
            !readOptional(target, "amsNetId", parsed.targetAmsNetId, error) ||
            !readOptional(target, "localIp", parsed.localIp, error) ||
            !readOptional(target, "localAmsNetId", parsed.localAmsNetId, error))
        {
            return false;
        }
        if (parsed.targetIp.empty())
        {
            error = "'target.ip' must not be empty";
            return false;
        }

        if (!readPort(ports, "systemServicePort", parsed.systemServicePort, error) ||
            !readRuntimePorts(ports, parsed.plcRuntimePorts, error) ||
            !readPort(ports, "ncConfigPort", parsed.ncConfigPort, error) ||
            !readPort(ports, "ncSymbolPort", parsed.ncSymbolPort, error) ||
            !readPort(ports, "etherCatMasterPort", parsed.etherCatMasterPort, error) ||
            !readPort(ports, "loggerPort", parsed.loggerPort, error) ||
            !readPort(ports, "adsTcpPort", parsed.adsTcpPort, error) ||
            !readPort(ports, "adsDiscoveryUdpPort", parsed.adsDiscoveryUdpPort, error))
        {
            return false;
        }

#define ADS_READ_BOOL(KEY) \
    if (!readOptional(behavior, #KEY, parsed.KEY, error)) \
        return false
        ADS_READ_BOOL(enableNcParameterWrites);
        ADS_READ_BOOL(enableCoeParameterWrites);
        ADS_READ_BOOL(enableEcStateRequests);
        ADS_READ_BOOL(enableAdsStateRequests);
        ADS_READ_BOOL(enableIcmpPingCheck);
        ADS_READ_BOOL(continueIfPingFails);
        ADS_READ_BOOL(enableAdsReachabilityCheck);
        ADS_READ_BOOL(enableAutoRouteDiagnostics);
        ADS_READ_BOOL(enableRouteCreationCommands);
        ADS_READ_BOOL(enableEventLoggerConnection);
        ADS_READ_BOOL(allowIndexOffsetFallback);
        ADS_READ_BOOL(allowSlowRecoveryRetry);
#undef ADS_READ_BOOL

        if (!readPositiveDouble(timing, "heartbeatPeriodSec", parsed.heartbeatPeriodSec, error) ||
            !readPositiveUnsigned(timing,
                                  "heartbeatFailureThreshold",
                                  parsed.heartbeatFailureThreshold,
                                  error) ||
            !readPositiveDouble(timing,
                                "routeCheckInitialBackoffSec",
                                parsed.routeCheckInitialBackoffSec,
                                error) ||
            !readPositiveDouble(timing,
                                "routeCheckMaxBackoffSec",
                                parsed.routeCheckMaxBackoffSec,
                                error) ||
            !readPositiveDouble(timing,
                                "bulkReadRetryWindowSec",
                                parsed.bulkReadRetryWindowSec,
                                error) ||
            !readPositiveDouble(timing,
                                "bulkWriteGroupWindowSec",
                                parsed.bulkWriteGroupWindowSec,
                                error) ||
            !readPositiveDouble(timing,
                                "slowRecoveryRetryPeriodSec",
                                parsed.slowRecoveryRetryPeriodSec,
                                error) ||
            !readPositiveUnsigned(
                timing, "adsRequestTimeoutMs", parsed.adsRequestTimeoutMs, error) ||
            !readPositiveUnsigned(limits, "maxBulkReadBatchSize", parsed.maxBulkReadBatchSize, error) ||
            !readPositiveUnsigned(
                limits, "maxBulkWriteBatchSize", parsed.maxBulkWriteBatchSize, error) ||
            !readPositiveUnsigned(
                limits, "maxFailedSymbolsPerCycle", parsed.maxFailedSymbolsPerCycle, error) ||
            !readPositiveUnsigned(limits,
                                  "maxEventLoggerMessagesPerSec",
                                  parsed.maxEventLoggerMessagesPerSec,
                                  error) ||
            !readPositiveUnsigned(
                limits, "maxDiagnosticLogRate", parsed.maxDiagnosticLogRate, error))
        {
            return false;
        }

        if (parsed.routeCheckMaxBackoffSec < parsed.routeCheckInitialBackoffSec)
        {
            error = "'routeCheckMaxBackoffSec' must be greater than or equal to "
                    "'routeCheckInitialBackoffSec'";
            return false;
        }
        if (parsed.maxBulkReadBatchSize > 500 || parsed.maxBulkWriteBatchSize > 500)
        {
            error = "bulk batch sizes must not exceed the driver's current 500-item limit";
            return false;
        }

        options = parsed;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool loadAdsIocConfig(const std::string& path, std::string& error)
{
    std::ifstream input(path.c_str());
    if (!input)
    {
        error = "unable to open TOML configuration file '" + path + "'";
        return false;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad())
    {
        error = "failed while reading TOML configuration file '" + path + "'";
        return false;
    }

    AdsIocOptions parsed;
    if (!parseAdsIocConfig(contents.str(), parsed, error))
    {
        error = "invalid ADS IOC config '" + path + "': " + error;
        return false;
    }

    std::lock_guard<std::mutex> guard(optionsMutex);
    loadedOptions = parsed;
    hasLoadedOptions = true;
    return true;
}

AdsIocOptions getAdsIocOptions()
{
    std::lock_guard<std::mutex> guard(optionsMutex);
    return loadedOptions;
}

void setAdsIocOptions(const AdsIocOptions& options)
{
    std::lock_guard<std::mutex> guard(optionsMutex);
    loadedOptions = options;
    hasLoadedOptions = true;
}

bool adsIocConfigIsLoaded()
{
    std::lock_guard<std::mutex> guard(optionsMutex);
    return hasLoadedOptions;
}
