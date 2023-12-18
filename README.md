# NetChan - Reliable Network Channels <img src="/imgs/netchan_logo.png" alt="NetChan Logo" width="150" height="auto" align="left">
Reliable, or deterministic **network channel** (*NetChan*) is a logical
construct that can be added to a distributed system to provide
deterministic and reliable connections. The core idea is to make it
simple to express the traffic for a channel in a concise, provable
manner and provide constructs for creating and using the channels.

In essence, a logical channel can be used as a local UNIX pipe between
remote systems.

A fundamental part of the reliability provided by NetChan is Time
Sensitive Networking (TSN) which is used to reserve capacity and guard
the data being transmitted. This is not to say that NetChan *require*

TSN, but without a hard QoS scheme, determinism cannot be guaranteed.

The motivation for NetChan is to make it easier to create large,
distributed systems without being bogged down in the minute details of
networking.
![Robot production](imgs/robot_production.png)
## Overall Design

**NetChan** is intended to create a logical channel between to processes
running on different hosts in a network. The process need not run on
different hosts (but if you're on the same host, perhaps other means of
communicating is a better fit).

Conceptually, NetChan provide a channel that is undisturbed by outside
events such as other people using **your** network capacity. A channel
is used by a sender (often called *Talker* in TSN terminology) and one
or more receivers (*Listeners*). The data in the channel is sent in a
*stream*, again from TSN terminology and is just a sequence of network
packages that logically belong together (think periodic temperature
readings or from a microphone being continously sampled).

![NetChan](imgs/net_chan.png)

Central to all of this, is *'the Manifest'* where all the streams are
listed.  Each stream is described using payload size, target
destination, traffic class, transmitting frequency and a unique ID. This
is then used by the core NetChan machinery to allocate buffers, start
receivers, reserve bandwidth etc.

```C
struct channel_attrs attrs[] = {
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x42},
		.stream_id = 42,
		.cs 	   = CLASS_A,
		.size      =  8,
		.freq      = 50,
		.name      = "mcast42",
	},
};
```

All participants in the distributed system will use the manifest to
configure the channels.

NetChan will compile a static library which can be used to link your
application. The project has a couple of examples, look at
[the meson build file](meson.build).


### Simple talker example
A talker (the task or process that **produces** data) need only 2 macros
from NetChan: namely
+ instantiate the channel locally
+ write data to channel

Note: this example is stripped of all excessive commands, includes
etc. Have a gander at [the talker](examples/talker.c) for the full example.
```C
#include <netchan.h>
int main(int argc, char *argv[])
{
	NETCHAN_TX(mcast42);
	for (int64_t i = 0; i < 10; i++) {
		WRITE(mcast42, &i);
		usleep(20000);
	}
	CLEANUP();
	return 0;
}
```

The examples directory also contains a sample listener to be used
alongside the talker example above.

### Simple C++ Listener example
Starting from v0.1.2, a C++ wrapper has been added to net_chan, exposing
Rx and Tx channels in a class hierarchy.

```C++
#include <netchan.hpp>
int main(int argc, char *argv[])
{
    netchan::NetHandler nh("eth0", "listener_log.csv", true);
    netchan::NetChanRx rx(nh, &attrs[0]);
    uint64_t data;
    while (1) {
        if (rx.read(&data)) {
            // handle data
        }
    }
}
```

## Build instructions

NetChan uses meson and ninja to build and details can be found in [the
meson build file](meson.build).

```bash
meson build
ninja -C build/
```

The C++ examples are not built by default as meson is not particularly
happy for building a C application and library and then also run a C++
compiler. For the time being, the C++ examples must be compiled
manually:

```bash
g++ -o build/cpp_listener examples/listener.cpp build/libnetchan.a build/libmrp.a -lboost_program_options -pthread -I include && \
g++ -o build/cpp_talker examples/talker.cpp build/libnetchan.a build/libmrp.a -lboost_program_options -pthread -I include && {
```

### Installing NetChan
To install, run ```meson install``` from within the build directory or
manually grab the generated files:
+ include/
+ build/libtimedavtp_avtp.a
+ build/libmrp.a


## Running tests

NetChan has a few unit-test written in
[Unity](http://www.throwtheswitch.org/unity), a lightweight C-framework
for tests that is reasonable small and (importantly) very fast. It does
require that an mrpd-daemon is running (see below). See
scripts/watch_builder.sh for how to set up tests to run.

A typical workflow consists of setting up watch_builder in one terminal
and watch the system kick into action when files are saved

```bash
./scripts/watch_builder.sh
~/dev/netchan ~/dev/netchan
. ./examples ./include ./include/srp ./src ./srp ./test ./tools
Setting up watches.
Watches established.

./ MODIFY README.md
ninja: Entering directory `build/'
ninja: no work to do.
ninja: Entering directory `/home/henrikau/dev/netchan/build'
ninja: no work to do.
1/5 mrp                OK              0.26s
2/5 nh macro           OK              0.47s
3/5 pdu test           OK              0.88s
4/5 nh net fifo        OK              0.91s
5/5 nh test            OK              1.13s


Ok:                 5
Expected Fail:      0
Fail:               0
Unexpected Pass:    0
Skipped:            0
Timeout:            0

Full log written to /home/henrikau/dev/netchan/build/meson-logs/testlog.txt
```

## Including Reliable Network Channels in other projects

The system builds 2 static libraries, one of which is a slightly
modified version of AvNUs mrp-client. This is kept separate to avoid
licensing issues. The other librarly (libnetchan.a) contains the
NetChan functionality.

The headers, apart from defining functions and #defines, also contains a
few helper-macros which is useful for testing the system. For a more
mature system, using the functions directly is recommended as you get a
bit more intuitive help from the compiler.

## Enabling TSN/AVB

To use TSN, a few components must be configured outside the project
### generalized Precision Timing Protocol (gPTP)

TSN relies on accurate timestamps and gPTP is a required
component. LinuxPTP is compatible with gPTP and comes with a gPTP
config. In this example the NIC is 'enp2s0', replace it with the correct
one for your network:

```bash
sudo ptp4l -i enp2s0 -f gPTP.cfg -m --step_threshold=1 --socket_priority=4
sudo phc2sys -s enp2s0 -c CLOCK_REALTIME --step_threshold=1 --transportSpecific=1 -w
```

There's no need to run phc2sys as we will read the timestamp directly
from the network interface.

ptp4l can either be installed via your favorite package manager or
cloned and built locally.
```bash
git clone git://git.code.sf.net/p/linuxptp/code linuxptp
cd linuxptp/
make
sudo make install
```

### Stream Reservation
A key feature of NetChan is the ability to reserve bandwidth and buffer
capacity through the network (provided the network supports TSN). This
is done using the stream reservation protocol (SRP) and AvNU has an
excellent project (OpenAvnu) to support this. In this project, the
*mrpd* daemon must be built and started locally *on each
machine*. NetChan comes with the client-side of AvNUs mrp code,
slightly tailored and adapted for our need (this can be found in the
srp/ subfolder). This allows NetChan to communicate with the
mrpd-daemon and send reservations, receive subscribe requests etc.

```bash
git clone github.com:Avnu/OpenAvnu.git
cd OpenAvnu
make mrpd
```

Once started, the dameon will then listen on a localhost port
awaiting clients to connect before sending the required SRP messages to
the network.

```bash
sudo ./daemons/mrpd/mrpd -i enp2s0 -mvs
```

#### Enabling SRP
To instruct NetChan to attach to the srp daemon and reserve bandwidth
and buffers, the startup-section of your code must contain calls to
```nc_use_srp();```. This will cause NetChan to hook into the
mrp-client library and ensure that the stream is properly protected. In
a network with other noise, this has a very noticable effect.
