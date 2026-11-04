#!c:/Repos/ads-ioc/R1.0.2///bin/windows-x64/adsIoc
################### AUTO-GENERATED DO NOT EDIT ###################
#
#         Project: TestIOC.tsproj
#        PLC name: TestIOC (TestIOC Instance)
# Generated using: pytmc 2.19.1
# Project version: unknown
#    Project hash: unknown
#     PLC IP/host: 172.21.148.154
#      PLC Net ID: 172.21.148.154.1.1
# ** DEVELOPMENT MODE IOC **
# * Using IOC boot directory for autosave.
# * Archiver settings will not be configured.
#
# Libraries:
#
#   LCLS General: * (SLAC)
#   LCLS Vacuum: * (SLAC - LCLS)
#   lcls-twincat-common-components: * (SLAC)
#   lcls-twincat-math: * (SLAC - LCLS)
#   lcls-twincat-motion: * (SLAC)
#   lcls-twincat-optics: * (SLAC)
#   lcls-twincat-physics: * (SLAC)
#   LCLS_MotionAbstraction: * (SLAC)
#   LCLS_Tc_DevAbs: * (SLAC - LCLS)
#   PMPS: * (SLAC - LCLS)
#   Tc2_MC2: * (Beckhoff Automation GmbH)
#   Tc2_Standard: * (Beckhoff Automation GmbH)
#   Tc2_System: * (Beckhoff Automation GmbH)
#   Tc3_Module: * (Beckhoff Automation GmbH)
#   TcUnit: * (www.tcunit.org)
#
################### AUTO-GENERATED DO NOT EDIT ###################
# Run common startup commands for linux soft IOC's
< $(IOC_COMMON)/All/pre_linux.cmd
< envPaths

epicsEnvSet("ADS_IOC_TOP", "$(TOP)" )

epicsEnvSet("ENGINEER", "" )
epicsEnvSet("LOCATION", "PLC:TestIOC" )
epicsEnvSet("IOCSH_PS1", "$(IOC)> " )
epicsEnvSet("ACF_FILE", "$(ADS_IOC_TOP)/iocBoot/templates/unrestricted.acf")

# Register all support components
dbLoadDatabase("$(ADS_IOC_TOP)/dbd/adsIoc.dbd")
adsIoc_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("ASYN_PORT",        "ASYN_PLC")
epicsEnvSet("IPADDR",           "172.21.148.154")
epicsEnvSet("AMSID",            "172.21.148.154.1.1")
epicsEnvSet("AMS_PORT",         "851")
epicsEnvSet("ADS_MAX_PARAMS",   "27755")
epicsEnvSet("ADS_SAMPLE_MS",    "50")
epicsEnvSet("ADS_MAX_DELAY_MS", "100")
epicsEnvSet("ADS_TIMEOUT_MS",   "1000")
epicsEnvSet("ADS_TIME_SOURCE",  "0")

# Add a route to the PLC automatically:
system("${ADS_IOC_TOP}/scripts/add_route.sh 172.21.148.154 ^172.*$")

# adsAsynPortDriverConfigure(portName, ipaddr, amsaddr, amsport,
#    asynParamTableSize, priority, noAutoConnect, defaultSampleTimeMS,
#    maxDelayTimeMS, adsTimeoutMS, defaultTimeSource,
#    ethercatMasterAmsNetIdOverride)
# portName            Asyn port name
# ipAddr              IP address of PLC
# amsaddr             AMS Address of PLC
# amsport             Default AMS port in PLC (851 for first PLC)
# paramTableSize      Maximum parameter/variable count. (1000)
# priority            Asyn priority (0)
# noAutoConnect       Enable auto connect (0=enabled)
# defaultSampleTimeMS Default sample of variable (PLC ams router
#                     checks if variable changed, if changed then add to send buffer) (50ms)
# maxDelayTimeMS      Maximum delay before variable that has changed is sent to client
#                     (Linux). The variable can also be sent sooner if the ams router
#                     send buffer is filled (100ms)
# adsTimeoutMS        Timeout for adslib commands (1000ms)
# defaultTimeSource   Default time stamp source of changed variable (PLC=0):
#                     PLC=0: The PLC time stamp from when the value was
#                         changed is used and set as timestamp in the EPICS record
#                         (if record TSE field is set to -2=enable asyn timestamp).
#                         This is the preferred setting.
#                     EPICS=1: The time stamp will be made when the updated data
#                         arrives in the EPICS client.
adsAsynPortDriverConfigure("$(ASYN_PORT)", "$(IPADDR)", "$(AMSID)", "$(AMS_PORT)", "$(ADS_MAX_PARAMS)", 0, 0, "$(ADS_SAMPLE_MS)", "$(ADS_MAX_DELAY_MS)", "$(ADS_TIMEOUT_MS)", "$(ADS_TIME_SOURCE)", "")

## Asyn/ADS diagnostics configuration (always loaded)
#define ASYN_TRACE_ERROR     0x0001
#define ASYN_TRACEIO_DEVICE  0x0002
#define ASYN_TRACEIO_FILTER  0x0004
#define ASYN_TRACEIO_DRIVER  0x0008
#define ASYN_TRACE_FLOW      0x0010
#define ASYN_TRACE_WARNING   0x0020
#define ASYN_TRACE_INFO      0x0040
asynSetTraceMask("$(ASYN_PORT)", -1, 0x41)

#define ASYN_TRACEIO_NODATA 0x0000
#define ASYN_TRACEIO_ASCII  0x0001
#define ASYN_TRACEIO_ESCAPE 0x0002
#define ASYN_TRACEIO_HEX    0x0004
asynSetTraceIOMask("$(ASYN_PORT)", -1, 2)

#define ASYN_TRACEINFO_TIME 0x0001
#define ASYN_TRACEINFO_PORT 0x0002
#define ASYN_TRACEINFO_SOURCE 0x0004
#define ASYN_TRACEINFO_THREAD 0x0008
asynSetTraceInfoMask("$(ASYN_PORT)", -1, 5)

#define AMPLIFIER_ON_FLAG_CREATE_AXIS  1
#define AMPLIFIER_ON_FLAG_WHEN_HOMING  2
#define AMPLIFIER_ON_FLAG_USING_CNEN   4

cd "$(ADS_IOC_TOP)/db"

########## Motor Configuration Block ##########
epicsEnvSet("MOTOR_PORT",     "PLC_ADS")
epicsEnvSet("PREFIX",         "PLC:TestIOC:")
epicsEnvSet("NUMAXES",        "52")
epicsEnvSet("MOVE_POLL_RATE", "200")
epicsEnvSet("IDLE_POLL_RATE", "1000")


# Create the EthercatMC controller for the legacy (ST_MotionStage) motors.
EthercatMCCreateController("$(MOTOR_PORT)", "$(ASYN_PORT)", "$(NUMAXES)", "$(MOVE_POLL_RATE)", "$(IDLE_POLL_RATE)")

epicsEnvSet("AXIS_NO",         "1")
epicsEnvSet("MOTOR_PREFIX",    "")
epicsEnvSet("MOTOR_NAME",      "$(PREFIX)M1")
epicsEnvSet("MOTOR_ADS_PATH",  "Main.M1")
epicsEnvSet("DESC",            "Main.M1 / Axis 1")
epicsEnvSet("EGU",             "mm")
epicsEnvSet("PREC",            "3")
epicsEnvSet("AXISCONFIG",      "")
epicsEnvSet("ECAXISFIELDINIT", "")
epicsEnvSet("AMPLIFIER_FLAGS", "")

# Create an EthercatMC axis instance for each legacy (ST_MotionStage) motor.
EthercatMCCreateAxis("$(MOTOR_PORT)", "$(AXIS_NO)", "$(AMPLIFIER_FLAGS)", "$(AXISCONFIG)")
dbLoadRecords("EthercatMC.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), R=$(MOTOR_NAME)-, MOTOR_PORT=$(MOTOR_PORT), ASYN_PORT=$(ASYN_PORT), AXIS_NO=$(AXIS_NO), DESC=$(DESC), PREC=$(PREC), EGU=$(EGU) $(ECAXISFIELDINIT)")
dbLoadRecords("EthercatMCreadback.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), R=$(MOTOR_NAME)-, MOTOR_PORT=$(MOTOR_PORT), ASYN_PORT=$(ASYN_PORT), AXIS_NO=$(AXIS_NO), DESC=$(DESC), PREC=$(PREC) ")
dbLoadRecords("EthercatMCdebug.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), MOTOR_PORT=$(MOTOR_PORT), AXIS_NO=$(AXIS_NO), PREC=3")

epicsEnvSet("AXIS_NO",         "2")
epicsEnvSet("MOTOR_PREFIX",    "")
epicsEnvSet("MOTOR_NAME",      "$(PREFIX)M2")
epicsEnvSet("MOTOR_ADS_PATH",  "Main.M2")
epicsEnvSet("DESC",            "Main.M2 / Axis 2")
epicsEnvSet("EGU",             "mm")
epicsEnvSet("PREC",            "3")
epicsEnvSet("AXISCONFIG",      "")
epicsEnvSet("ECAXISFIELDINIT", "")
epicsEnvSet("AMPLIFIER_FLAGS", "")

# Create an EthercatMC axis instance for each legacy (ST_MotionStage) motor.
EthercatMCCreateAxis("$(MOTOR_PORT)", "$(AXIS_NO)", "$(AMPLIFIER_FLAGS)", "$(AXISCONFIG)")
dbLoadRecords("EthercatMC.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), R=$(MOTOR_NAME)-, MOTOR_PORT=$(MOTOR_PORT), ASYN_PORT=$(ASYN_PORT), AXIS_NO=$(AXIS_NO), DESC=$(DESC), PREC=$(PREC), EGU=$(EGU) $(ECAXISFIELDINIT)")
dbLoadRecords("EthercatMCreadback.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), R=$(MOTOR_NAME)-, MOTOR_PORT=$(MOTOR_PORT), ASYN_PORT=$(ASYN_PORT), AXIS_NO=$(AXIS_NO), DESC=$(DESC), PREC=$(PREC) ")
dbLoadRecords("EthercatMCdebug.template", "PREFIX=$(MOTOR_PREFIX), MOTOR_NAME=$(MOTOR_NAME), MOTOR_PORT=$(MOTOR_PORT), AXIS_NO=$(AXIS_NO), PREC=3")


dbLoadRecords("iocSoft.db", "IOC=PLC:TestIOC")
dbLoadRecords("save_restoreStatus.db", "P=PLC:TestIOC:")
dbLoadRecords("caPutLog.db", "IOC=$(IOC)")

## TwinCAT task, application, and project information databases ##
dbLoadRecords("TwinCAT_TaskInfo.db", "PORT=$(ASYN_PORT),PREFIX=PLC:TestIOC,IDX=1,TASK_PORT=350")
dbLoadRecords("TwinCAT_AppInfo.db", "PORT=$(ASYN_PORT), PREFIX=PLC:TestIOC")

dbLoadRecords("TwinCAT_Project.db", "PREFIX=PLC:TestIOC,PROJECT=TestIOC.tsproj,HASH=unknown,VERSION=unknown,PYTMC=2.19.1,PLC_HOST=172.21.148.154")

#   LCLS General: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=LCLS_General,VERSION=*,VENDOR=SLAC")
#   LCLS Vacuum: * (SLAC - LCLS)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=LCLS_Vacuum,VERSION=*,VENDOR=SLAC - LCLS")
#   lcls-twincat-common-components: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=lcls-twincat-common-components,VERSION=*,VENDOR=SLAC")
#   lcls-twincat-math: * (SLAC - LCLS)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=lcls-twincat-math,VERSION=*,VENDOR=SLAC - LCLS")
#   lcls-twincat-motion: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=lcls-twincat-motion,VERSION=*,VENDOR=SLAC")
#   lcls-twincat-optics: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=lcls-twincat-optics,VERSION=*,VENDOR=SLAC")
#   lcls-twincat-physics: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=lcls-twincat-physics,VERSION=*,VENDOR=SLAC")
#   LCLS_MotionAbstraction: * (SLAC)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=LCLS_MotionAbstraction,VERSION=*,VENDOR=SLAC")
#   LCLS_Tc_DevAbs: * (SLAC - LCLS)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=LCLS_Tc_DevAbs,VERSION=*,VENDOR=SLAC - LCLS")
#   PMPS: * (SLAC - LCLS)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=PMPS,VERSION=*,VENDOR=SLAC - LCLS")
#   Tc2_MC2: * (Beckhoff Automation GmbH)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=Tc2_MC2,VERSION=*,VENDOR=Beckhoff Automation GmbH")
#   Tc2_Standard: * (Beckhoff Automation GmbH)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=Tc2_Standard,VERSION=*,VENDOR=Beckhoff Automation GmbH")
#   Tc2_System: * (Beckhoff Automation GmbH)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=Tc2_System,VERSION=*,VENDOR=Beckhoff Automation GmbH")
#   Tc3_Module: * (Beckhoff Automation GmbH)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=Tc3_Module,VERSION=*,VENDOR=Beckhoff Automation GmbH")
#   TcUnit: * (www.tcunit.org)
dbLoadRecords("TwinCAT_Dependency.db", "PREFIX=PLC:TestIOC,DEPENDENCY=TcUnit,VERSION=*,VENDOR=www.tcunit.org")

cd "$(IOC_TOP)"

## PLC Project Database files ##
dbLoadRecords("TestIOC.db", "PORT=$(ASYN_PORT),PREFIX=PLC:TestIOC:,IOCNAME=$(IOC),IOC=$(IOC)")

# Total records: 26755
callbackSetQueueSize(55510)

# Autosave and archive settings:
save_restoreSet_status_prefix("PLC:TestIOC:")
save_restoreSet_IncompleteSetsOk(1)
save_restoreSet_DatedBackupFiles(1)
set_pass0_restoreFile("info_positions.sav")
set_pass1_restoreFile("info_settings.sav")

# ** Development IOC Settings **
# Development IOC autosave and archive files go in the IOC top directory:
cd "$(IOC_TOP)"

# (Development mode) Create info_positions.req and info_settings.req
makeAutosaveFiles()
# (Development mode) Create the archiver file
makeArchiveFromDbInfo("$(IOC).archive", "archive")

# Configure access security: this is required for caPutLog.
asSetFilename("$(ACF_FILE)")

# Initialize the IOC and start processing records
iocInit()

# Enable logging
iocLogInit()

# Configure and start the caPutLogger after iocInit
epicsEnvSet(EPICS_AS_PUT_LOG_PV, "$(IOC):caPutLog:Last")

# caPutLogInit("HOST:PORT", config)
# config options:
#       caPutLogNone       -1: no logging (disable)
#       caPutLogOnChange    0: log only on value change
#       caPutLogAll         1: log all puts
#       caPutLogAllNoFilter 2: log all puts no filtering on same PV
caPutLogInit("$(EPICS_CAPUTLOG_HOST):$(EPICS_CAPUTLOG_PORT)", 0)

# Start autosave backups
create_monitor_set( "info_positions.req", 10, "" )
create_monitor_set( "info_settings.req", 60, "" )
