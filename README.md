EPICS Module to communicate with TwinCAT controllers over ADS protocol.

Developed for use with EEE (ESS EPICS Environment) but can also be compiled for "normal" EPICS

git clone --recursive https://bitbucket.org/europeanspallationsource/m-epics-twincat-ads

TwinCAT plc source example  can be found in TwinCAT/demo_project/

Start EPICS ioc with:
iocsh adsOnlyIO.cmd
iocsh adsMotorRecord.cmd

EtherCAT slave-state request writes use ADS index group `0x0009` and a 16-bit
state value. The driver accepts `INIT` (`0x01`), `PREOP` (`0x02`),
`BOOTSTRAP` (`0x03`), `SAFEOP` (`0x04`), and `OP` (`0x08`) for this group and
rejects unsupported values or data sizes. Project-generated IOC records use
the master's ADS port (`65535`) and should remain disabled by default at the
EPICS record layer.

For EtherCAT state records, the driver discovers the EtherCAT master AMS
Net ID through the PLC's ADS R0 I/O device list. It uses the discovered ID
when exactly one configured I/O device is present, or derives the conventional
`.2.1` suffix from the PLC AMS Net ID otherwise. Set the IOC environment
variable `ECAT_MASTER_AMS_NET_ID` before `adsAsynPortDriverConfigure` to
override discovery.

Contact: anders.sandstrom@esss.se
