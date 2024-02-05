#!/usr/bin/env python3
import os
import sys
import math
import pandas as pd
import numpy as np
import argparse
import datetime
import statistics
import matplotlib.pyplot as plt
from PIL import Image
from PIL.PngImagePlugin import PngInfo
from pathlib import Path
import NetChan_Stream



if __name__ == "__main__":
    parser = argparse.ArgumentParser("Analyze logfiles from manychan runs and split out into individual StreamID-logs")
    parser.add_argument("-f", "--folder", required=True, help="Folder of logfiles")
    parser.add_argument("-b", "--file_base", required=False, default="", help="Base-name of logfiles to parse (i.e. 'test1_1k' for 'test1_10-1.csv', 'test1_10k-2.csv' etc")
    parser.add_argument("-s", "--stream_id", required=False, help="Only filter out provided StreamID")
    args = parser.parse_args()

    # Find matching files
    folder = Path(args.folder)
    files =  [f for f in folder.glob(f"*{args.file_base}*") if os.path.isfile(f)]

    df = {}
    for f in files:
        print(f"Reading {f}")
        _df = pd.read_csv(f)
        df[f.stem] = _df

    if not args.stream_id:
        streams = NetChan_Stream.find_all_streams(df)
    else:
        streams = [int(args.stream_id)]
    sthex = [f"{hex(s)}" for s in streams]
    for s,sx in zip(streams, sthex):
        print(f"Found stream: {s} : {sx}")

    base = Path(args.file_base).stem
    for sid in streams:
        print(f"{'-'*80}\nfiltering StreamID {sid}")
        target = Path(folder, f"filtered_{sid}.csv")
        target_dropped   = Path(folder, f"filtered_{sid}_dropped.csv")
        target_corrupted = Path(folder, f"filtered_{sid}_corrupted.csv")

        df_sid,df_dropped,df_corrupted = NetChan_Stream.merge_stream(df, sid)
        print("Summary:")
        print(f"Found    : {len(df_sid):10d} packets")
        print(f"Dropped  : {len(df_dropped):10d} ({100.0 * len(df_dropped)/(len(df_sid)+len(df_dropped)+len(df_corrupted)):.3f} %)")
        print(f"Corrupted: {len(df_corrupted):10d} ({100.0 * len(df_corrupted)/(len(df_sid)+len(df_dropped)+len(df_corrupted)):.3f} %)")
        print("\n")
        df_sid.to_csv(target)
        df_dropped.to_csv(target_dropped)
        df_corrupted.to_csv(target_corrupted)
