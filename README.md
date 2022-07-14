# WiPerf

WiPerf is a Wi-Fi performance monitoring framework developed to collect georeferenced network performance data in heterogeneous wireless environments - with multiple Wi-Fi access networks of different standards - to support network selection by vehicles and other mobile clients. This tool can simultaneously collect data from 802.11n, 802.11ac, and 802.11ad Wi-Fi networks.

It is composed by three primary modules:
 * **Throughput Module:** Measure the maximum throughput by sending pseudo-random UDP data.
 * **Channel Monitor Module:** Collect channel state information from wireless interface drivers.
 * **GPS Module:** Gather real-time GPS mobility information (e.g., speed and heading).

## Instructions

The tool was developed and tested in TP-Link Talon AD7200 routers, running a version of the LEDE v17.02 operating system specifically built for such devices and available at [LEDE for Talon AD7200](https://github.com/seemoo-lab/lede-ad7200).

Since WiPerf relies on GPS information, the equipment must have access to a GPS receiver installed in a serial port (e.g., /dev/ttyACM0).

### Compilation

For compilation, the program relies on environment variables with paths to LEDE/OpenWRT packages and libraries. Below is an example with the required variables and associated values.

```bash
export PATH=/path/to/openwrt/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi/bin:$PATH
export STAGING_DIR=/path/to/openwrt/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi
export TARGET_DIR=/path/to/openwrt/staging_dir/target-arm_cortex-a15+neon-vfpv4_musl-1.1.16_eabi
export TARGET_LIBS=/path/to/openwrt/staging_dir/target-arm_cortex-a15+neon-vfpv4_musl-1.1.16_eabi/usr/lib
```

With the variables configured, you can compile the program from the root of the 'src' folder by running:

```bash
make ARCH=arm
```

### Configuration

To use the software you must first add the configure file as '/etc/wiperf.conf'. Below is an example of a configuration file and how to modify it.

```bash
[gpsinfo]
# path to the shared memory segment where the GPS information will be kept
shm-path = /wiperf-gpsinfo

[mygpsd]
# path to the serial port where the GPS receiver is connect to, for the GPS
# module to receive and process the GPS NMEA messages
serial-device = /dev/ttyACM0
log-level = 2

[database]
# PostgreSQL database parameters to access it
db-name = wiperf_database
host = localhost
user = user
password = password

[data-sender]
# interface names, IP addresses and port number that must be used to send
# UDP pseudo-random data to measure the throughput
ifaces = wlan0 10.0.0.1, wlan1 10.0.1.1, wlan2 10.0.2.1
port = 44443
log-level = 4
decision-level = 0

[data-receiver]
# interface names, IP address and port number to where the UDP packets will
# be sent
ifaces = wlan0 10.0.0.4, wlan1 10.0.1.4, wlan2 10.0.2.4
port = 44444
log-level = 4

[feedback-sender]
# Interface, IP address and port number used to transmit the feedback messages
# containing the throughput information
ifaces = wlan0 10.0.0.4
port = 44445
log-level = 4
# number of milliseconds between feedback messages
feedback-interval = 1000

[feedback-receiver]
# Interface, IP address and port number to where the feedback messages are sent
ifaces = wlan0 10.0.0.1
port = 44446
log-level = 4
# number of milliseconds between feedback messages
feedback-interval = 1000

[channel-monitor]
# Names of the interfaces from which channel state information will be collected
ifaces = wlan0, wlan1, wlan2
# number of milliseconds between each sampling instance
sampling-interval = 1000
```

### Running WiPerf

After compiling and configuring the necessary files, you can initiate the tool by running the following commands in separate CLI terminals:

#### Access Point
```bash
# cd /path/to/wintech2022-wiperf/src/mygpsd
# ./mygpsd
```
```bash
# cd /path/to/wintech2022-wiperf/src/dtransfer/dreceiver
# ./dreceiver
```

#### Mobile Client 
```bash
# cd /path/to/wintech2022-wiperf/src/mygpsd
# ./mygpsd
```
```bash
# cd /path/to/wintech2022-wiperf/src/dtransfer/dsender
# ./dsender
```
```bash
# cd /path/to/wintech2022-wiperf/src/dtransfer/channelMonitor
# ./channel_monitor
```

## Feedback

The software is under development. If you find any issues, feel free add a new issue or to please contact us via e-mail.

## Contact

Daniel Teixeira <<dfteixeira@fe.up.pt>>

## Institutions and Funding

<a href="https://www.it.pt/">![IT logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/it.png)</a> &nbsp;
<a href="https://sigarra.up.pt/feup/">![FEUP logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/feup.png)</a> &nbsp;
<a href="https://www.fct.pt/">![FCT logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/fct.png)</a>&nbsp;
<a href="https://www.compete2020.gov.pt/">![COMPETE2020 logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/compete2020.png)</a>&nbsp;
<a href="https://portugal2020.pt/">![PT2020 logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/pt2020.png)</a>&nbsp;
<a href="https://www.europarl.europa.eu/factsheets/en/sheet/95/el-fondo-europeo-de-desarrollo-regional-feder-">![FEDER logo](https://github.com/danielfilipeteixeira24/wintech2022-wiperf/blob/main/logos/ue.png)</a>&nbsp;
