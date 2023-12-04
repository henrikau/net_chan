#!/usr/bin/env python3
import sys
import argparse
import statistics
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def filter_sids(dft, dfl, sids):
    """Dataframe captured from both Talker and Receiver, filter out the desired streams.

    Note that the distinction between talker and listener is somewhat
    blurred if both ES have multiple streams going different directions.
    """
    dfs = {}
    for sid in sids:
        print(f"Merging StreamID {sid:d}")
        df_t = dft[dft['stream_id']==sid]
        df_l = dfl[dfl['stream_id']==sid]

        # Decide if rx or tx should be dropped (Tx and Rx may have
        # multiple channels going different ways)
        if (df_t['rx_ns'] == 0).all():
            df_t = df_t.drop(['rx_ns', 'recv_ptp_ns'], axis=1)
            df_l = df_l.drop(['cap_ptp_ns', 'send_ptp_ns', 'tx_ns'], axis=1)
            df = pd.merge(df_t, df_l, on=['stream_id', 'avtp_ns', 'seqnr', 'sz'], suffixes=['_l', '_t'])
        else:
            df_l = df_l.drop(['rx_ns', 'recv_ptp_ns'], axis=1)
            df_t = df_t.drop(['cap_ptp_ns', 'send_ptp_ns', 'tx_ns'], axis=1)
            df = pd.merge(df_t, df_l, on=['stream_id', 'avtp_ns', 'seqnr', 'sz'], suffixes=['_l', '_t'])
        # Adjust for LEAP second adjustment (CLOCK_REALTIME is 37 seconds ahead)
        df['send_ptp_ns'] += int(37*1e9)
        df['tx_ns'] += int(37*1e9)
        df['rx_ns'] += int(37*1e9)

        df['cap2tx']  = df['tx_ns'] - df['cap_ptp_ns']
        df['tx2rx']   = df['rx_ns'] - df['tx_ns']
        df['rx2recv'] = df['recv_ptp_ns'] - df['rx_ns']

        df['e2e'] = df['recv_ptp_ns'] - df['cap_ptp_ns']
        df['tx_diff'] = df['tx_ns'].diff()
        df['rx_diff'] = df['rx_ns'].diff()

        # Rolling average (simple trending)
        smalen = max(10, int(len(df) * 0.05))
        df['e2e_sma'] = df['e2e'].rolling(window=smalen).mean()
        df['tx_diff_sma'] = df['tx_diff'].rolling(window=smalen).mean()
        df['rx_diff_sma'] = df['rx_diff'].rolling(window=smalen).mean()

        # when using diff, first row will be useless, so drop it from the set
        df.drop(index=df.index[0], axis=0, inplace=True)
        dfs[sid] = df
    return dfs


def print_statistics(sid, df):
    print(f"{'-'*9}+{'-'*20}+{'-'*20}+{'-'*20}+")
    print(f"{sid:^9}|{'E2E':^20s}|{'Tx Period':^20s}|{'Rx Period':^20s}|")
    print(f"{'-'*9}+{'-'*20}+{'-'*20}+{'-'*20}+")
    print(f"{'Max':8s} | {max(df['e2e'])/1e3:15.3f} us | {max(df['tx_diff'])/1e3:15.3f} us | {max(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'Min':8s} | {min(df['e2e'])/1e3:15.3f} us | {min(df['tx_diff'])/1e3:15.3f} us | {min(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'Average':8s} | {statistics.mean(df['e2e'])/1e3:15.3f} us | {statistics.mean(df['tx_diff'])/1e3:15.3f} us | {statistics.mean(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'StdDev':8s} | {statistics.stdev(df['e2e'])/1e3:15.3f} us | {statistics.stdev(df['tx_diff'])/1e3:15.3f} us | {statistics.stdev(df['rx_diff'])/1e3:15.3f} us |")


def plot_e2e(ax, df, title):
    ax.set_title(title)
    # x = range(len(df))
    x = df['cap_ptp_ns'].values / 1e9
    x = x - x[0]
    ax.set_xlim((0, x[-1]))
    ax.plot(x, df['e2e'], color='green', alpha=0.4)
    ax.plot(x, df['e2e_sma'], color='red')

def plot_tx_period(ax, df, title):
    ax.set_title(title)
    # counts, bins = np.histogram(df['tx_diff']/1000, bins=10)
    # counts = counts / len(df['tx_diff'])
    # ax.set_xlim((0, bins[-1]))
    # ax.hist(bins[:-1], bins, weights=counts)
    x = range(len(df['tx_diff']))
    ax.set_xlim((0, x[-1]))
    ax.plot(x, df['tx_diff'], color='green', alpha=0.4)
    ax.plot(x, df['tx_diff_sma'], color='red')



def plot_rx_period(ax, df, title):
    ax.set_title(title)
    counts, bins = np.histogram(df['rx_diff']/1000, bins=10)
    print(df['rx_diff'])
    counts = counts / len(df['rx_diff'])
    ax.set_xlim((0, bins[-1]))
    ax.hist(bins[:-1], bins, weights=counts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser("Helper for analyzing testruns using netchans built-in logging facilities. "\
                                     "It expects a talker.csv and a listener.csv (not the .csv_d files!) " \
                                     "It will give a total E2E delay, both ways")
    parser.add_argument("-t", "--talker"  , required=True, help="file path talker .csv file w/timestamps")
    parser.add_argument("-l", "--listener", required=True, help="file path listener .csv file w/timestamps")
    parser.add_argument("-s", "--stream_id", required=True, type=int, action='append', help="list of stream-ids to analyze")
    parser.add_argument("-o", "--out_file", help="File to save plot to")
    args = parser.parse_args()

    # Read and merge data
    dft = pd.read_csv(args.talker)
    dfl = pd.read_csv(args.listener)
    dfs = filter_sids(dft, dfl, args.stream_id)
    print(f"Got {len(dfs)} streams filtered and ready")

    for sid in dfs.keys():
        print_statistics(sid, dfs[sid])

    figsize=(16,9)
    dpi = 80
    if args.out_file:
        figsize=(24,13.5)
        dpi = 160
    fig = plt.figure(num=None, figsize=figsize, dpi=dpi, facecolor='w', edgecolor='k')
    # Determine size of subplot
    rows = 3
    cols = len(dfs)
    idx = 1
    for sid in dfs.keys():
        plot_e2e(fig.add_subplot(rows, cols, idx), dfs[sid], f"E2E StreamID={sid}")
        plot_tx_period(fig.add_subplot(rows, cols, cols + idx), dfs[sid], f"TxPeriod StreamID={sid}")
        plot_rx_period(fig.add_subplot(rows, cols, 2*cols + idx), dfs[sid], f"RxPeriod StreamID={sid}")
        idx += 1

    if args.out_file:
        fig.savefig(args.out_file)
