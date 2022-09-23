#!/usr/bin/env python3
import os
import sys
import argparse
import matplotlib.pyplot as plt

sys.path.append("/home/henrikau/dev/ipynb/")
import lib.timedc_lib as tcl
import lib.packet_delay as pkt
import lib.WakeDelay as wkd
import lib.NetNoise as noise
import lib.gPTP as gptp

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

    ax1.set_title(title)
    ax1.set_xlim((ts_first, ts_last))
    ax1.set_xlabel("time")
    ax1.set_ylabel("Frame E2E delay ($\mu$s)")
    ax1.set_ylim((0,int(p.e2e_max*1.15/1000.0)))
    ax1.tick_params(axis='y', colors='green')

    ax1.plot(p.m['time_ns_rel']/1e9, p.m['e2e_ns']/1000.0, 'o', color='green', alpha=0.3, label="E2E latency ($\mu$s)")
    ax1.plot(p.m['time_ns_rel']/1e9, p.m['throughput']/1000.0, 'x', color='blue', label="Dropped frame")
    ax1.plot([-1], [-1], color='red', label="Netw. noise") # hack to get on legend
    if show_legend:
        ax1.legend(loc='upper right')

    ax2 = ax1.twinx()
    ax2.set_ylabel("Mbps")
    ax2.plot(noise_ts, noise_bw, color='red')
    ax2.set_ylim((0, 1000))
    ax2.tick_params(axis='y', colors='red')

def analyze_5min():
    path="/home/henrikau/dev/timedc_work/code/net_chan_profiling_22/varnoise_e2e_5min"
    pkt_nosrp = pkt.PktDelay("{}/netfifo_listener_rt_nosrp.csv".format(path),
                            "{}/netfifo_talker_rt_nosrp.csv".format(path),
                             "Listener", "Talker", True)
    wkd_nosrp = wkd.WakeDelay("{}/netfifo_listener_rt_nosrp.csv_d".format(path),
                              "{}/netfifo_talker_rt_nosrp.csv_d".format(path),
                              "Listener", "Talker")

    pkt_srp = pkt.PktDelay("{}/netfifo_listener_rt_srp.csv".format(path),
                            "{}/netfifo_talker_rt_srp.csv".format(path),
                             "Listener", "Talker", True)
    wkd_srp = wkd.WakeDelay("{}/netfifo_listener_rt_srp.csv_d".format(path),
                              "{}/netfifo_talker_rt_srp.csv_d".format(path),
                              "Listener", "Talker")

    pkt_nosrp.set_title("VarNoise E2E Linux v5.16.2-rt19, Short run (RT, make+noise)")
    pkt_srp.set_title("VarNoise E2E Linux v5.16.2-rt19, Short run (RT+SRP, make+noise)")
    wkd_nosrp.set_title("VarNoise E2E Linux v5.16.2-rt19, Short run (RT, make+noise)")
    wkd_srp.set_title("VarNoise E2E Linux v5.16.2-rt19, Short run (RT+SRP, make+noise)")

    print(pkt_nosrp)
    print(pkt_srp)
    print(wkd_nosrp)
    print(wkd_srp)

    wkd_nosrp.set_folder(path, "5min_rt_nosrp_preempt_rt_make")
    wkd_srp.set_folder(path, "5min_rt_srp_preempt_rt_make")
    wkd_nosrp.plot_all("5.16.2-rt19, make, RT")
    wkd_srp.plot_all("5.16.2-rt19, make, RT+SRP")
    with open("{}/e2e_delay_noise.log".format(path), "w") as f:
        f.write("\nPacket Delay analysis. 5min run, w/periodic netnoise, NO SRP\n")
        f.write(str(pkt_nosrp))
        f.write("\nPacket Delay analysis. 5min run, w/periodic netnoise, With SRP\n")
        f.write(str(pkt_srp))


def analyze_60min():
    path="/home/henrikau/dev/timedc_work/code/net_chan_profiling_22/varnoise_e2e_60min"

    pkt_srp = pkt.PktDelay("{}/netfifo_listener_rt_srp.csv".format(path),
                           "{}/netfifo_talker_rt_srp.csv".format(path),
                           "Listener", "Talker", True)
    pkt_srp.set_title("VarNoise E2E Linux v5.16.2-rt19, longrun (RT+SRP, make+noise)")

    tr21wkd = wkd.WakeDelay("{}/netfifo_listener_rt_srp.csv_d".format(path),
                            "{}/netfifo_talker_rt_srp.csv_d".format(path),
                            "Listener", "Talker")
    tr21wkd.set_title("VarNoise E2E Linux v5.16.2-rt19, longrun (RT+SRP, make+noise)")
    tr21gptp = gptp.gPTP("{}/gptp_rt_srp_listener.log".format(path),
                         "{}/gptp_rt_srp_talker.log".format(path),
                         "Listener", "Talker")

    print(pkt_srp)

    print(tr21wkd)

    with open("{}/e2e_delay_noise.log".format(path), "w") as f:
        f.write("\nPacket Delay analysis. 60 min run, w/periodic netnoise, With SRP\n")
        f.write(str(pkt_srp))

    tr21wkd.set_folder(path, "rt_srp_preempt_rt_make")
    tr21wkd.plot_all("5.16.2-rt19, make, RT")

    n_srp = noise.NetNoise("{}/tr21_netnoise_srp.log".format(path), "Netnoise, SRP, 5 min cycle", -12)
    fig_srp = plt.figure(num=None, figsize=(24,6.75), dpi=160, facecolor='w', edgecolor='k')
    plt.tight_layout()
    do_plot(fig_srp.add_subplot(111), n_srp, pkt_srp, "Frame E2E ($\mu$s) with stream protection")
    fig_srp.savefig("{}/e2e_delay_noise_srp.png".format(path), bbox_inches='tight')


if __name__ == "__main__":
    analyze_5min()
    analyze_60min()
