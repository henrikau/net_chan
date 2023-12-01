#!/usr/bin/env python3
import sys
import argparse
import statistics
import pandas as pd
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
        df['e2e_sma'] = df['e2e'].rolling(window=100).mean()
        df['tx_diff'] = df['tx_ns'].diff()
        df['tx_diff_sma'] = df['tx_diff'].rolling(window=100).mean()
        df['rx_diff'] = df['rx_ns'].diff()
        df['rx_diff_sma'] = df['rx_diff'].rolling(window=100).mean()
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



if __name__ == "__main__":
    parser = argparse.ArgumentParser("Helper for analyzing testruns using netchans built-in logging facilities. "\
                                     "It expects a talker.csv and a listener.csv (not the .csv_d files!) " \
                                     "It will give a total E2E delay, both ways")
    parser.add_argument("-t", "--talker"  , required=True, help="file path talker .csv file w/timestamps")
    parser.add_argument("-l", "--listener", required=True, help="file path listener .csv file w/timestamps")
    parser.add_argument("-s", "--stream_id", required=True, type=int, action='append', help="list of stream-ids to analyze")
    args = parser.parse_args()

    # Read and merge data
    dft = pd.read_csv(args.talker)
    dfl = pd.read_csv(args.listener)
    dfs = filter_sids(dft, dfl, args.stream_id)
    print(f"Got {len(dfs)} streams filtered and ready")

    for sid in dfs.keys():
        print_statistics(sid, dfs[sid])
