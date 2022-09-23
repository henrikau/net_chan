#!/usr/bin/env python3
import os
import sys

sys.path.append("/home/henrikau/dev/ipynb/")
from lib import *
import lib.timedc_lib as tcl
from lib.gPTP import *

base = "/home/henrikau/dev/timedc_work/code/net_chan_profiling_22/ptp_profiling"

if __name__ == "__main__":
    gptpl = "{}/gptp_rt_srp_listener.log".format(base)
    gptpt = "{}/gptp_rt_srp_talker.log".format(base)
    gptp = gPTP(gptpl, gptpt, "Listener", "Talker")

    print(gptp.table_summary("5.16.2-rt19", "RT+SRP"))

    gptp.set_folder(base, "gptp_rms_rt_srp")
    gptp.plot_all("5.16.2-rt19, rt+SRP, noise", rms_only=True)
