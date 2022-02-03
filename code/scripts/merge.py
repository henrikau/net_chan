#!/usr/bin/env python3
import pandas as pd
import argparse
import sys
import os

def main(txf, rxf, outf):
    dft = pd.read_csv(txf)
    dfr = pd.read_csv(rxf)
    df_merged = pd.merge(dfr, dft, on=['stream_id', 'sz', 'seqnr', 'avtp_ns'])

    df_merged = df_merged.rename(columns={'rx_ns_x'      : 'rx_ns',
                                          'recv_ptp_ns_x': 'recv_ptp_ns',
                                          'cap_ptp_ns_y' : 'cap_ptp_ns',
                                          'send_ptp_ns_y': 'send_ptp_ns'})
    df_merged = df_merged.drop(columns=['cap_ptp_ns_x', 'send_ptp_ns_x', 'rx_ns_y', 'recv_ptp_ns_y'])
    df_merged.to_csv(outf)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Merge timestamps from timedc')
    parser.add_argument('-t', '--txlog', dest='txfile')
    parser.add_argument('-r', '--rxlog', dest='rxfile')
    parser.add_argument('-o', '--outfile', dest='outfile')
    args = parser.parse_args()
    if not args.txfile or not args.rxfile or not args.outfile:
        print("Need 2 files to merge and a result to store into")
        sys.exit(1)
    if not os.path.isfile(args.txfile):
        print("Cannt find {}".format(args.txfile))
        sys.exit(1)
    if not os.path.isfile(args.rxfile):
        print("Cannt find {}".format(args.rxfile))
        sys.exit(1)
    main(args.txfile, args.rxfile, args.outfile)

