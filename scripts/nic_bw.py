#!/usr/bin/env python3
import argparse
import math
import sys

VERBOSE = False
ETHER = 18 + 4  # Ethernet header + CRC + VLAN
L1 = 7 + 1 + 12 # Preamble, start, IPG
CSHDR = 24
MIN_PAYLOAD = ETHER+CSHDR

def do_print(line):
    if VERBOSE:
        print(line)

"""

Provided a path to a manifest file, this will calculate the bandwith
parameter.

Based on 802.1Q Annex L, class A should be OK, B requires more work to
verify.

"""
def print_channels(channels):
    """
    Pretty print all channels found
    """
    if not channels:
        return
    # Split by channels
    cha = [channels[x] for x in channels.keys() if channels[x]['sc'] == 'CLASS_A']
    chb = [channels[x] for x in channels.keys() if channels[x]['sc'] == 'CLASS_B']
    print(" Class | StreamID |     Name     |  Hz  | sz")
    print("-------+----------+--------------+------+---------")
    for ch in cha:
        print(" {:^5s} | {:^8s} | {:>12s} | {:>4s} | {}".format("A", ch['stream_id'], ch['name'], ch['freq'], ch['size']))
    for ch in chb:
        print(" {:^5s} | {:^8s} | {:>12s} | {:>4s} | {}".format("B", ch['stream_id'], ch['name'], ch['freq'], ch['size']))



def valid(block):
    if not block:
        return False
    if 'name' not in block or 'size' not in block or 'freq' not in block:
        return False
    return True

  
def calc_bw_req(channel):
    """
    Find BW requirements for a single channel"
    """
    # Minimum payloadsize for ethernet is 46 bytes, this includes any protocol header 
    # (like the AVTP header)
    # For netchan, this is Common Transport Header, 24 bytes.
    # AVB operates with worst-case sample (i.e. for 125us interval for 48kHz sampling, one should reserve 
    # for 7 sample-sets pr frame), we ignore this here.
    payload = int(channel['size']) + CSHDR
    if payload < MIN_PAYLOAD:
        payload = MIN_PAYLOAD
    size = payload + ETHER + L1
    dps = size * int(channel['freq']) * 8
    idle_slope = dps / 1e6
    
    do_print("calc_bw_req()")
    for k in channel.keys():
        do_print("{:12s}: {}".format(k, channel[k]))
    do_print("\t{:12s}: {}".format("Header overhead", ETHER+L1))
    do_print("\t{:12s}: {}".format("Payload data", channel['size']))
    do_print("\t{:12s}: {}".format("Full payload size", payload))
    do_print("\t{:12s}: {}".format("Frame-size", size))
    do_print("\t{:12s}: {:.3f} %".format("SnR", 100.0 * float(channel['size']) / size))
    do_print("\t{:12s}: {}".format("dps", dps))
    do_print("\t{:12s}: {}".format("idleSlope", dps / 1e6))
    do_print("")
    return int(math.ceil(dps / 1000))

def line_clean(line):
    # strip away comments
    if "/*" in line and "*/" in line:
        start = line.find("/*")
        end = line.rfind("*/")
        line = line[:start] + line[(end+2):]
    if "//" in line:
        line = line[:line.find("//")]
    return line.strip()


def parse_file(manifest):
    channels = {}
    block = {}
    found_channel_attrs = False
    lvlctr = 0
    # grab each channel, hope the file is properly structured
    with open(manifest, 'r') as f:
        for l in f.readlines():
            line = line_clean(l)
            if not found_channel_attrs:
                if "struct channel_attrs" in line:
                    found_channel_attrs = True
                    do_print("Found beginning of struct")
                continue
            else:
                # Star/end of a block, update ctr accordingly to find
                # when to dump a complete block into channels
                if '{' in line:
                    lvlctr += 1
                    do_print("new block -> {}".format(lvlctr))
                if '}' in line:
                    lvlctr -= 1
                    if block:
                        if valid(block):
                            if block['name'] in channels:
                                print("Duplicate name found in manifest! {} seen more than once".format(block['name']))
                            else:
                                channels[block['name']] = block

                        else:
                            print("Invalid channelblock detected!")
                            print("This won't be added, please have a look at {}".format(manifest))
                            print("incomplete block: ")
                            print(block)

                    block = {}
                    do_print("end of block -> {}".format(lvlctr))

                # In a valid block, grab key and token and populate block
                if lvlctr > 0:
                    if line.startswith('.'):
                        k = line.split('=')[0][1:].strip(' \"\t')
                        v = line.split('=')[-1][:-1].strip(' \"\t')
                        block[k] = v


            if lvlctr < 0:
                do_print("End of struct")
                found_channel_attrs = False
                break
    return channels

def main(channels, txstreams, linkspeed, verbose):
    txs = [x.strip() for x in txstreams.split(',')]
    kbps_a = 0
    kbps_b = 0
    for t in txs:
        if t not in channels:
            print("Did not find {} in channels!".format(t))
        else:
            if channels[t]['sc'] == 'CLASS_A':
                kbps_a += calc_bw_req(channels[t])
            else:
                kbps_b += calc_bw_req(channels[t])

    # accumulated credtis in kilobits / sec
    idleSlope_a = kbps_a
    # idleSlope_a = 20000         # for debugging

    # Sanitycheck of idleSlope. If too small, set to 1. Normally we have
    # multiple streams, so the accumulated BW-req for the NIC will be
    # higher, but in the case of a single stream, we have to help it a
    # little.
    #
    # Note: could be that ETF is the Qdisc you want
    if idleSlope_a <= 0:
        print("!!!                                                      !!!")
        print("!!! Idleslope too small, rounding error, increasing to 1 !!!")
        print("!!!                                                      !!!")
        print("!!!       Perhaps ETF is really what you want?           !!!")
        print("!!!                                                      !!!")
        print("")
        idleSlope_a = 1

    # rate of depleting credits when transmitting. Factoring in as
    # idleslope will continue to replenish credits.
    # In kbit/sec
    sendSlope_a = int(int(linkspeed) - idleSlope_a)

    # max credit that can be saved when a frame is ready to Tx and
    # blocked by other traffic
    interferenceTime_a = 1500.0 / int(linkspeed)
    hiCredit_a = int(idleSlope_a * interferenceTime_a)

    # minimum number of credits that can be reached during tx of a frame
    loCredit_a = int(sendSlope_a * interferenceTime_a)

    # Largets interference from this queue (required for B)
    maxBurstSize_a = int(linkspeed * (hiCredit_a - loCredit_a)/(-sendSlope_a))


    # Now that we have all of Class A's values, we can find B. See
    # L3.1.1 in 802.1Q-2018
    idleSlope_b = kbps_b + idleSlope_a
    sendSlope_b = int(int(linkspeed) - idleSlope_b)
    interferenceTime_b = 1500.0 / (int(linkspeed) - idleSlope_a) + interferenceTime_a
    hiCredit_b = int(idleSlope_b * interferenceTime_b)
    loCredit_b = int(sendSlope_b  * interferenceTime_b)
    maxBurstSize_b = 0

    print("{}".format("="*80))
    print("Found {} channels in manifest".format(len(channels)))
    print("Linkspeed: {:.2f} Mbps".format(int(linkspeed) / 1000.0))
    print("CBS Class A:")
    print("\tbandwidth: {:8d} kbps".format(kbps_a))
    print("\tidleslope: {:8d}".format(idleSlope_a))
    print("\tsendSlope: {:8d}".format(sendSlope_a))
    print("\thiCredit:  {:8d}".format(hiCredit_a))
    print("\tloCredit:  {:8d}".format(loCredit_a))
    print("\tmaxBurstSize:  {:8d}".format(maxBurstSize_a))
    print("\tmaxIntereferenceTime: {:.3f} ms".format(interferenceTime_a * 1000))
    print("\tBW reqs: {:8f}\%".format(idleSlope_a * 100.0 / int(linkspeed)))
    print("")


    print("CBS Class B:")
    print("\tbandwidth: {:5d} kbps".format(kbps_b))
    print("\tidleslope: {:8d}".format(idleSlope_b))
    print("\tsendSlope: {:8d}".format(sendSlope_b))
    print("\thiCredit:  {:8d}".format(hiCredit_b))
    print("\tloCredit:  {:8d}".format(loCredit_b))
    print("\tmaxBurstSize:  {:8d}".format(maxBurstSize_b))
    print("\tmaxIntereferenceTime: {:.3f} ms".format(interferenceTime_b * 1000))
    print("\tBW reqs: {:8f}\%".format(idleSlope_b * 100.0 / int(linkspeed)))
    print("")

    print("{}".format("="*80))
    print("Commands to run on host (use correct nic and parent!")
    print("\texport CBSNIC=enp2s0")
    print("\tsudo tc qdisc replace dev ${CBSNIC} parent root mqprio num_tc 4 \\")
    print("\t\tmap 3 3 1 0 2 2 2 2 2 2 2 2 2 2 2 2 queues 1@0 1@1 1@2 1@3 hw 0")
    print("\tsudo tc qdisc (add|replace) dev {} parent {} cbs idleslope {} sendslope -{} \\".\
          format("${CBSNIC}", "8003:1", idleSlope_a, sendSlope_a))
    print("\t\thicredit {} locredit -{} offload 1".format(hiCredit_a, loCredit_a))
    print("\tsudo tc qdisc (add|replace) dev {} parent {} cbs idleslope {} sendslope -{} \\".\
          format("${CBSNIC}", "8003:2", idleSlope_b, sendSlope_b))
    print("\t\thicredit {} locredit -{} offload 1".format(hiCredit_b, loCredit_b))

    print("to inspect:")
    print("tc -g class show dev enp2s0")
    print("tc -s -d qdisc show dev enp2s0")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Calculate idleSlope, sendSlope, hiCredit and loCredit based on argumentlist and manifest.')
    parser.add_argument('-m', '--manifest',   dest='manifest')
    parser.add_argument('-v', '--verbose' ,   action="store_true", default=False)
    parser.add_argument('-t', '--txstreams',  dest='txstreams',   help="name of outgoing streams on this system")
    parser.add_argument('-l', '--list',       action="store_true", default=False, help="Provide a (short)list of all streams declared in the manifest")
    parser.add_argument('-L', '--link-speed', dest='linkspeed',   help="Speed of link we're configuring in kbit/s.", default=1000000)
    args = parser.parse_args()
    VERBOSE = args.verbose

    if not args.manifest:
        print("Need manifest-file to parse")
        parser.print_help(sys.stderr)
        sys.exit(1)
    
    channels = parse_file(args.manifest)
    if args.list:
        print_channels(channels)
    else:
        main(channels, args.txstreams, args.linkspeed, args.verbose)
