# Reliable Network Channels

This project creates network channels, which if wanted, can be
configured to use AVB/TSN for network protection. Currently only AVB
class A and B is supported.

Typically we refer to the project as 'net chans' or 'net_chan'.

## Overall Design

Net_chan is intended to create a logical channel between to processes
running on different hosts in a network. The process need not run on
different hosts (but if you're on the same host, perhaps other means of
communicating is a better fit?).

Conceptually, net_chan provide a channel that is undisturbed by outside
events such as other people yousing **your** network capacity.

![net_chan](imgs/net_chan.png)


Central to all of this, is 'the manifest'. This is how we describe the
channels we use, adding metrics such as payload size, target
destination, traffic class, transmitting frequency and a unique ID for
the stream.

```C
struct net_fifo net_fifo_chans[] = {
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x42},
		.stream_id = 42,
		.class	   = CLASS_A,
		.size      =  8,
		.freq      = 50,
		.name      = "mcast42",
	},
};
```

All participants in the distributed system you create must share this
file as both talker and listener will configure the available channels
based on this content.

### Simple talker example
A talker (the task or process that **produces** data) need only 2 macros
from net_chan: namely
+ instantiate the channel locally
+ write data to channel

Note: this example is stripped of all excessive commands, includes
etc. Have a gander at [the talker](examples/talker.c) for a more comprehensive example.
```C
int main(int argc, char *argv[])
{
	NETFIFO_TX(mcast42);
	for (int64_t i = 0; i < 10; i++) {
		WRITE(mcast42, &i);
		usleep(20000);
	}
	CLEANUP();
	return 0;
}
```


## Build instructions

net_chan uses meson and ninja to build

```bash
meson build
ninja -C build/
```

### Installing net_chan
To install, run ```meson install``` from within the build directory or
manually grab the generated files:
+ include/
+ build/libtimedavtp_avtp.a
+ build/libmrp.a


### Notes
Note: in the **very** near future, libtimedavtp_avtp.a will be renamed
along with a substantial rewrite of the API. The current naming-scheme
is the result of this project being initially targeted for Timed C, an
extension to C which adds time as a primitive. As net_chan is a more
generic construct, we aim to move this out from under the umbrella of
Timed C.

## Running tests

net_chan has a few unit-test written in Unity, a C-framework for tests
that is reasonable small and (importantly) very fast. It does require
that an mrpd-daemon is running (see below). See scripts/watch_builder.sh
for how to set up tests to run.

A typical workflow consists of setting up watch_builder in one terminal
and watch the system kick into action when files are saved

```bash
./scripts/watch_builder.sh
~/dev/net_chan ~/dev/net_chan
. ./examples ./include ./include/srp ./src ./srp ./test ./tools
Setting up watches.
Watches established.

./ MODIFY README.md
ninja: Entering directory `build/'
ninja: no work to do.
ninja: Entering directory `/home/henrikau/dev/net_chan/build'
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

Full log written to /home/henrikau/dev/net_chan/build/meson-logs/testlog.txt
```


## Including Reliable Network Channels in other projects

The system builds 2 static libraries, one of which is a slightly
modified version of AvNUs mrp-client. This is kept separate to avoid
licensing issues. The other librarly (libtimedc_avtp.a) contains the
net_chan functionality.

The headers, apart from definig functions and #defines, also contains a
few helper-macros which is useful for testing the system. For a more
mature system, using the functions directly is recommended as you get a
bit more intuitive help from the compiler.

## Enabling TSN/AVB

To use TSN, a few components must be configured outside the project
### generalized Precision Timing Protocol (gPTP)

TSN relies on accurate timestamps and gPTP is a required
component. LinuxPTP is compatible with gPTP and comes with a gPTP config

```bash
sudo ptp4l -i enp2s0 -f gPTP.cfg -m
```

There's no need to run phc2sys as we will read the timestamp directly
from the network interface, but if you choose to run phc2sys, it will
not harm anything.

ptp4l can either be installed via your favorite package manager or
cloned and built locally
```bash
git clone git://git.code.sf.net/p/linuxptp/code linuxptp
cd linuxptp/
make
sudo make install
```

### Stream Reservation
A key feature of TSN is the reservation of bandwidth and buffer capacity
through the network. AvNU has an excellent project (OpenAvnu) which
contains code for the mrpd daemon. net_chan comes with the client-side
of AvNUs mrp code, slightly tailored and adapted for our need (this can
be found in the srp/ subfolder).

```bash
git clone github.com:Avnu/OpenAvnu.git
cd OpenAvnu
make mrpd
```

Before running net_chan, the daemon must be started and attached to the
network that supports MRP. It will then listen on a localhost port
awaiting clients to connect before sending the required SRP messages to
the network.

```bash
sudo ./daemons/mrpd/mrpd -i enp2s0 -mvs
```

### Enabling SRP
To instruct net_chan to attach to the srp daemon and reserve bandwidth
and buffers, the startup-section of your code must contain calls to
```nf_use_srp();```. This will cause net_chan to hook into the
mrp-client library and ensure that the stream is properly protected. In
a network with other noise, this has a very noticable effect.
