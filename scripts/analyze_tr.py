#!/usr/bin/env python3
import sys
import argparse
import statistics
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.patches as mp
import datetime

def parse_invalid_periods(periods):
    if not periods:
        return None
    res = []
    for period in args.invalid_period:
        s,e = period.split(',')
        start = int(s.split('(')[1])*1000000000
        end =int(e.split(')')[0])*1000000000
        res.append((min(start,end), max(start, end)))
    if not res:
        print("no periods given")
        return None

    return res


def parse_period(interval_ms):
    if not interval_ms:
        return None
    lower_ns = int(interval_ms.split(',')[0]) * 1000000
    upper_ns = int(interval_ms.split(',')[1]) * 1000000
    return (min(lower_ns, upper_ns), max(lower_ns, upper_ns))


def get_dropped_range(df, filter, key):
    # create a dataset for dropped timestamps (needed for
    # indicating in plot later.
    dr = []
    dr_idx = df.index[~filter].tolist()
    for idx in dr_idx:
        # avoid out of bounds
        if idx >= len(df) - 1:
            break
        dr.append((df[key].iloc[idx], df[key].iloc[idx+1]))

    # for each pair, combine adjacent dropped pairs
    dr_range = []

    prev_s = dr[0][0]
    prev_e = dr[0][1]
    for idx in range(1, len(dr)):
        # if currents start is previous's end, we have a
        # continous region, so extend prev
        if dr[idx][0] == prev_e:
            prev_e = dr[idx][1]
            # extend the diff
        else:
            dr_range.append((prev_s, prev_e))
            prev_s = dr[idx][0]
            prev_e = dr[idx][1]
        dr_range.append((prev_s, prev_e)) # add final value

    return dr_range


def filter_sids(dft, dfl, sids, invalid_periods=None, e2e_limit=None, period_limit=None):
    """Dataframe captured from both Talker and Receiver, filter out the desired streams.

    Note that the distinction between talker and listener is somewhat
    blurred if both ES have multiple streams going different directions.
    """
    dfs = {}

    # Keep dropped in an array. We could keep it split between each SID,
    # but once the clock is unstable or someone is way out of bounds, we
    # should not trust values found in the other streams either.
    dropped = []

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

        # set diff limit to x * NS_IN_SEC
        NS_IN_MSEC = 1000000
        diff_limit = 500 * NS_IN_MSEC

        df['e2e'] = df['recv_ptp_ns'] - df['cap_ptp_ns']
        df['tx_diff'] = df['tx_ns'].diff()
        df['rx_diff'] = df['rx_ns'].diff()

        # Filter out invalid periods (if provided)
        if period_limit:
            period_filter = (df['tx_diff'] > period_limit[0]) & (df['tx_diff'] < period_limit[1]) & (df['rx_diff'] > period_limit[0]) & (df['tx_diff'] < period_limit[1])
            dropped.extend(get_dropped_range(df, period_filter, 'cap_ptp_ns'))
            df = df[period_filter]

        # Filter out e2e, only keep those in the designated valid interval
        print("Filtering E2E limit")
        if e2e_limit:
            l = len(df)
            filter = (df['e2e'] > e2e_limit[0]) & (df['e2e'] < e2e_limit[1])
            dropped.extend(get_dropped_range(df, filter, 'cap_ptp_ns'))

            # Finally, drop invalid entries from the dataset
            df = df[filter]


        # Filter out invalid intervals reported by PTP parser (-I switch)
        if invalid_periods:
            print(f"Got {len(invalid_periods)} invalid periods, filtering..")
            for start, end in invalid_periods:
                df = df[(df['cap_ptp_ns'] < start) | (df['cap_ptp_ns'] > end)]

        # Rolling average (simple trending)
        smalen = max(10, int(len(df) * 0.05))
        print(f"Using windowlength={smalen} for SMA")
        df['e2e_sma'] = df['e2e'].rolling(window=smalen).mean()
        df['tx_diff_sma'] = df['tx_diff'].rolling(window=smalen).mean()
        df['rx_diff_sma'] = df['rx_diff'].rolling(window=smalen).mean()

        # when using diff, first row will be useless, so drop it from the set
        df.drop(index=df.index[0], axis=0, inplace=True)
        dfs[sid] = df

    return dfs, dropped


def get_duration(df):
    """
    Find the first and last timestamp and find how long the timeseries is (as a str)
    """
    start = int(df['cap_ptp_ns'].iloc[0])
    end = int(df['recv_ptp_ns'].iloc[-1])
    return str(datetime.timedelta(seconds=(end-start)/1e9))



def print_statistics(sid, df):
    print(f"StreamID={sid} Duration={get_duration(df)} count={len(df)}")
    print(f"{'-'*9}+{'-'*20}+{'-'*20}+{'-'*20}+")
    print(f"{sid:^9}|{'E2E':^20s}|{'Tx Period':^20s}|{'Rx Period':^20s}|")
    print(f"{'-'*9}+{'-'*20}+{'-'*20}+{'-'*20}+")
    print(f"{'Max':8s} | {max(df['e2e'])/1e3:15.3f} us | {max(df['tx_diff'])/1e3:15.3f} us | {max(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'Min':8s} | {min(df['e2e'])/1e3:15.3f} us | {min(df['tx_diff'])/1e3:15.3f} us | {min(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'Average':8s} | {statistics.mean(df['e2e'])/1e3:15.3f} us | {statistics.mean(df['tx_diff'])/1e3:15.3f} us | {statistics.mean(df['rx_diff'])/1e3:15.3f} us |")
    print(f"{'StdDev':8s} | {statistics.stdev(df['e2e'])/1e3:15.3f} us | {statistics.stdev(df['tx_diff'])/1e3:15.3f} us | {statistics.stdev(df['rx_diff'])/1e3:15.3f} us |")


def add_invalid_interval(ax, y, y_len, periods, color='red'):
    if not periods:
        return

    for s,e in periods:
        s = s/1e9
        e = e/1e9
        ax.add_patch(mp.Rectangle((s, y), (e-s), y_len, alpha=0.1, color=color))


def plot_e2e(ax, df, title, invalid_periods=None, invalid_e2e=None):
    ax.set_title(title)
    x = df['cap_ptp_ns'].values / 1e9

    # max/min y-axis
    max_y = max(df['e2e']/1e3)
    min_y = min(df['e2e']/1e3)

    add_invalid_interval(ax, min_y, max_y-min_y, invalid_periods, color='red')
    add_invalid_interval(ax, min_y, max_y-min_y, invalid_e2e, color='yellow')

    ax.set_xlim((x[0], x[-1]))
    ax.set_ylim((min_y, max_y))
    ax.plot(x, df['e2e']/1e3, color='green', alpha=0.4)
    ax.plot(x, df['e2e_sma']/1e3, color='blue')


    ax.set_ylabel('E2E $\mu$s')
    ax.set_xlabel("Capture time")

def plot_tx_period(ax, df, title, invalid_periods=None, invalid_e2e=None):
    ax.set_title(title)

    # counts, bins = np.histogram(df['tx_diff']/1000, bins=10)
    # counts = counts / len(df['tx_diff'])
    # ax.set_xlim((0, bins[-1]))
    # ax.hist(bins[:-1], bins, weights=counts)

    # x = range(len(df['tx_diff']))
    x = df['cap_ptp_ns'].values / 1e9
    max_y = max(df['tx_diff']/1e6)
    min_y = min(df['tx_diff']/1e6)
    add_invalid_interval(ax, min_y, max_y - min_y, invalid_periods, color='red')
    add_invalid_interval(ax, min_y, max_y - min_y, invalid_e2e, color='yellow')

    ax.plot(x, df['tx_diff']/1e6, color='green', alpha=0.4)
    ax.plot(x, df['tx_diff_sma']/1e6, color='blue')

    ax.set_xlim((x[0], x[-1]))
    # ax.set_ylim((min_y, max_y))
    ax.set_ylabel("Tx Period (ms)")
    ax.ticklabel_format(useOffset=False)



def plot_rx_period(ax, df, title, invalid_periods=None, invalid_e2e=None):
    # ax.set_title(title)
    # counts, bins = np.histogram(df['rx_diff']/1000, bins=20)
    # counts = counts / len(df['rx_diff'])
    # ax.set_xlim((bins[0], bins[-1]*1.05))
    # ax.stairs(counts, bins)
    # ax.set_xlabel("Rx period distribution ($\mu$s)")
    x = df['cap_ptp_ns'].values / 1e9
    max_y = max(df['rx_diff']/1e6)
    min_y = min(df['rx_diff']/1e6)

    add_invalid_interval(ax, min_y, max_y - min_y, invalid_periods, color='red')
    add_invalid_interval(ax, min_y, max_y - min_y, invalid_e2e, color='yellow')

    ax.plot(x, df['rx_diff']/1e6, color='green', alpha=0.4)
    ax.plot(x, df['rx_diff_sma']/1e6, color='blue')

    ax.set_xlim((x[0], x[-1]))
    # ax.set_ylim((min_y, max_y))
    ax.set_ylabel("Rx Period (ms)")
    ax.ticklabel_format(useOffset=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser("Helper for analyzing testruns using netchans built-in logging facilities. "\
                                     "It expects a talker.csv and a listener.csv (not the .csv_d files!) " \
                                     "It will give a total E2E delay, both ways")
    parser.add_argument("-t", "--talker"  , required=True, help="file path talker .csv file w/timestamps")
    parser.add_argument("-l", "--listener", required=True, help="file path listener .csv file w/timestamps")
    parser.add_argument("-s", "--stream_id", required=True, type=int, action='append', help="list of stream-ids to analyze")
    parser.add_argument("-o", "--out_file", help="File to save plot to")
    parser.add_argument("-I", "--invalid_period", required=False, action='append', help="Period (in TAI seconds) where the clock was unstable due to PTP synch errors (from analyze_ptp_syslog.py)")
    parser.add_argument("-E", "--valid_e2e_interval", required=False, help="Interval (ms) with valid E2E times. Typically between 0 and 10ms for TSN traffic. To filter series with clock error(s)")
    parser.add_argument("-P", "--valid_period_interval", required=False, help="Interval (ms) for which we consider Rx|Tx period to be valid.")

    args = parser.parse_args()
    invalid_periods = parse_invalid_periods(args.invalid_period)
    e2e_interval = parse_period(args.valid_e2e_interval)
    period_interval = parse_period(args.valid_period_interval)
    # Read and merge data
    dft = pd.read_csv(args.talker)
    dfl = pd.read_csv(args.listener)
    dfs, dropped = filter_sids(dft, dfl, args.stream_id, invalid_periods, e2e_interval, period_interval)
    print(f"Got {len(dfs)} streams filtered and ready")

    ts = False
    for sid in dfs.keys():
        lts = dfs[sid]['cap_ptp_ns'].iloc[0]
        if not ts or lts < ts:
            ts = lts
        print_statistics(sid, dfs[sid])

    figsize=(16,9)
    dpi = 80
    if args.out_file:
        figsize=(24,13.5)
        dpi = 160
    fig = plt.figure(num=None, figsize=figsize, dpi=dpi, facecolor='w', edgecolor='k')
    # Determine size of subplot

    cap_time = str(datetime.datetime.fromtimestamp(int(lts/1e9)))
    plt.suptitle(f"Traffic analysis, {len(dfs)} streams, starting at {cap_time}")
    rows = 3
    cols = len(dfs)
    idx = 1
    for sid in dfs.keys():
        plot_e2e(fig.add_subplot(rows, cols, idx), dfs[sid], f"E2E StreamID={sid}", invalid_periods, dropped)
        plot_tx_period(fig.add_subplot(rows, cols, cols + idx), dfs[sid], f"TxPeriod StreamID={sid}", invalid_periods, dropped)
        plot_rx_period(fig.add_subplot(rows, cols, 2*cols + idx), dfs[sid], f"RxPeriod StreamID={sid}", invalid_periods)
        idx += 1

    plt.tight_layout()
    if args.out_file:
        fig.savefig(args.out_file)
