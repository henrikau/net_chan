#!/usr/bin/env python3
import os
import re
import sys
import math
import pandas as pd
import argparse
import datetime
import statistics
import matplotlib.pyplot as plt
from PIL import Image
from pathlib import Path
import NetChan_Stream


if __name__ == "__main__":
    parser = argparse.ArgumentParser("Analyze logfile from manychan runs")
    parser.add_argument("-f", "--file", action='append', help="logfile to analyze")
    parser.add_argument("-d", "--directory", help="Directory of logfiles (on the form 'filtered_<stream_id>.csv')")
    parser.add_argument("-o", "--out_file", help="File to save plot to")
    parser.add_argument("--e2e_title", default=None, type=str, help="Extra title for the E2E plot")
    parser.add_argument("--title", default=None, type=str, help="Extra information for the main title of the plot")
    args = parser.parse_args()

    files = []
    if args.file:
        for f in args.file:
            files.append(Path(f))
    if args.directory:
        folder = Path(args.directory)
        files.extend([f for f in os.listdir(folder) if re.search(r'^filtered_[\d]+\.csv$', f)])

    # Read all files and store
    ncs = []
    for f in files:
        print(f"Looking at {f}")
        try:
            # If the file is too short, NetChan_Stream throws a ValueError
            # Just ignore the file and continue processing.
            ncs.append(NetChan_Stream.NetChan_Stream(f))
        except ValueError:
            pass

    if not args.out_file and len(ncs) > 1:
        print(f"Not saving to file, we have {len(ncs)} streams to analyze, not sure this is what you want.")
        sys.exit(0)

    for nc in ncs:
        print(nc)
        figsize=(16,9)
        dpi = 80
        if args.out_file:
            # 32,16 seems to be a good match for filling out the entire
            # 4k screen (3820x2160). Why the ratio needs to be 2 rather
            # than 1.78 I do not know
            figsize=(32,18)
            dpi = 160
        fig = plt.figure(num=None, figsize=figsize, dpi=dpi, facecolor='w', edgecolor='k')

        nc.set_title(fig, args.title)
        nc.plot_e2e(fig.add_subplot(411), args.e2e_title, show_dropped=True)
        nc.plot_e2e_hist(fig.add_subplot(423))
        nc.plot_e2e_hist(fig.add_subplot(424), filter_std=3)
        nc.plot_periods(fig.add_subplot(425), rx=True)
        nc.plot_periods(fig.add_subplot(426), rx=False)
        nc.plot_periods_hist(fig.add_subplot(427), rx=True)
        nc.plot_periods_hist(fig.add_subplot(428), rx=False)

        fig.tight_layout()
        plt.subplots_adjust(top=0.92, hspace=0.3)
        if args.out_file:
            fname =""
            if args.directory:
                fname = f"{Path(args.directory)}/"
            fname += f"{Path(args.out_file).stem}_{nc.get_figname()}"

            try:
                print(f"Saving plot to {fname}")
                fig.savefig(fname, bbox_inches='tight')
                target_img = Image.open(fname)
                target_img.save(fname, pnginfo=nc.get_metadata())
                print(f"Saved to {fname}")
            except:
                print(f"Failed saving image, should probably investigate..")
        else:
            print(nc)
            plt.show()
