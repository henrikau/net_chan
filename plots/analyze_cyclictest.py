#!/usr/bin/env python3
import os
import sys
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import statistics
sys.path.append("/home/henrikau/dev/ipynb/")
import lib.timedc_lib as tcl
import lib.CyclicTest as ct



if __name__ == "__main__":
    base = "/home/henrikau/dev/timedc_work/code/net_chan_profiling_22/cyclictest"
    kernel1="5.10.0-11-amd64"
    kernel2="5.16.2-rt19"
    tsn1="{}/cyclictest_tsn1_{}_2.log".format(base, kernel1)
    tsn2="{}/cyclictest_tsn2_{}_2.log".format(base, kernel2)

    print("tsn1 (Listener): {}".format(tsn1))
    print("tsn2 (Talker): {}".format(tsn2))
    t1 = ct.read_cyclictest_fast(tsn1)
    t2 = ct.read_cyclictest_fast(tsn2)
    print(ct.get_stats(t1, kernel1))
    print(ct.get_stats(t2, kernel2))
