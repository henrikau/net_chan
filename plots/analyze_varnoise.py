#!/usr/bin/env python3
import os
import sys
import argparse
import matplotlib.pyplot as plt

bpath = os.path.dirname(os.path.dirname(sys.argv[0]))
sys.path.append(os.path.dirname(sys.argv[0])+"/lib/")

import lib.timedc_lib as tcl
import lib.packet_delay as pkt
import lib.WakeDelay as wkd
import lib.NetNoise as noise

SMALL_SIZE = 12
MEDIUM_SIZE = 17
BIGGER_SIZE = 24

plt.rc('font', size=SMALL_SIZE)          # controls default text sizes
plt.rc('axes', titlesize=MEDIUM_SIZE)     # fontsize of the axes title
plt.rc('axes', labelsize=MEDIUM_SIZE)    # fontsize of the x and y labels
plt.rc('xtick', labelsize=SMALL_SIZE)    # fontsize of the tick labels
plt.rc('ytick', labelsize=SMALL_SIZE)    # fontsize of the tick labels
plt.rc('legend', fontsize=MEDIUM_SIZE)    # legend fontsize
plt.rc('figure', titlesize=50)  # fontsize of the figure title

def do_plot(ax1, n, p, title="", show_legend=True):
    # packets has time in ns, move to sec
    pkt_ts = p.m['time_ns_rel']/1e9
    pkt_tp = p.m['throughput']

    # restrict start/end of noise
    # drop all items in n lower than pkt_ts[0]
    ts_first = pkt_ts.iloc[0]
    ts_last = pkt_ts.iloc[-1]

    # Noise has time in sec
    noise_ts = n.df['time_rel']
    noise_bw = n.df['bwmbps']

    ax1.set_title(title, y=1.0, pad=-22, fontsize=BIGGER_SIZE)
    ax1.set_xlim((ts_first, ts_last))
    ax1.set_xlabel("Time (sec)")
    ax1.set_ylabel("E2E delay ($\mu$s)")
    ax1.set_ylim((0,int(p.e2e_max*1.15/1000.0)))
    ax1.tick_params(axis='y', colors='green')

    ax1.plot(p.m['time_ns_rel']/1e9, p.m['e2e_ns']/1000.0, 'o', color='green', alpha=0.3, label="E2E ($\mu$s)")
    ax1.plot(p.m['time_ns_rel']/1e9, p.m['throughput']/1000.0, 'x', color='blue', label="Dropped")
    ax1.plot([-1], [-1], color='red', label="Noise") # hack to get on legend
    if show_legend:
        ax1.legend(loc='upper right')

    ax2 = ax1.twinx()
    ax2.set_ylabel("Mbps")
    ax2.plot(noise_ts, noise_bw, color='red')
    ax2.set_ylim((0, 1000))
    ax2.tick_params(axis='y', colors='red')


base = "{}/net_chan_profiling_22/varnoise".format(bpath)
if __name__ == "__main__":
    # No SRP
    n1 = noise.NetNoise("{}/netnoise_nosrp.log".format(base), "Netnoise, No SRP, 30 sec cycles", 2.8)
    p1 = pkt.PktDelay("{}/netfifo_listener_rt_nosrp.csv".format(base),
                      "{}/netfifo_talker_rt_nosrp.csv".format(base),
                      "Listener", "Talker", True)
    print("Time offset: {}, {} -> {}".format(
        p1.m['cap_ptp_ns_t'][0], n1.df['ts'][0], p1.m['cap_ptp_ns_t'][0] - n1.df['ts'][0]))


    # Using SRP
    n2 = noise.NetNoise("{}/netnoise_srp.log".format(base), "Netnoise, SRP, 30 sec cycles", -1.35)
    p2 = pkt.PktDelay("{}/netfifo_listener_rt_srp.csv".format(base),
                      "{}/netfifo_talker_rt_srp.csv".format(base),
                      "Listener", "Talker", True)
    # plt.rcParams.update({'font.size': 12})
    fig = plt.figure(num=None, figsize=(24,13.5), dpi=160, facecolor='w', edgecolor='k')
    fig_nosrp = plt.figure(num=None, figsize=(24,6.75), dpi=160, facecolor='w', edgecolor='k')
    fig_srp = plt.figure(num=None, figsize=(24,6.75), dpi=160, facecolor='w', edgecolor='k')
    plt.tight_layout()

    do_plot(fig.add_subplot(211), n1, p1, "No SRP", False)
    do_plot(fig.add_subplot(212), n2, p2, "SRP")

    do_plot(fig_nosrp.add_subplot(111), n1, p1, "Frame E2E ($\mu$s), no stream protection")
    do_plot(fig_srp.add_subplot(111), n2, p2, "Frame E2E ($\mu$s) with stream protection")

    fig_nosrp.savefig("{}/e2e_delay_noise_nosrp.png".format(base), bbox_inches='tight')
    fig_srp.savefig("{}/e2e_delay_noise_srp.png".format(base), bbox_inches='tight')
    fig.savefig("{}/e2e_delay_noise.png".format(base), bbox_inches='tight')
    with open("{}/e2e_delay_noise.log".format(base), "w") as f:
        f.write(str(n1))
        f.write(str(n2))

        f.write("\nPacket Delay analysis. 5min run, w/periodic netnoise, NO SRP\n")
        f.write(str(p1))
        f.write("\nPacket Delay analysis. 5min run, w/periodic netnoise, With SRP\n")
        f.write(str(p2))
