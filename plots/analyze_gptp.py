#!/usr/bin/env python3
import os
import sys

bpath = os.path.dirname(os.path.dirname(sys.argv[0]))
sys.path.append(os.path.dirname(sys.argv[0])+"/lib/")

from lib.gPTP import *
from lib import *
import lib.timedc_lib as tcl

print(bpath)
base = "{}/net_chan_profiling_22/ptp_profiling".format(bpath)

if __name__ == "__main__":
    gptpl = "{}/gptp_rt_srp_listener.log".format(base)
    gptpt = "{}/gptp_rt_srp_talker.log".format(base)
    gptp = gPTP.gPTP(gptpl, gptpt, "Listener", "Talker")

    print(gptp.table_summary("5.16.2-rt19", "RT+SRP"))

    gptp.set_folder(base, "gptp_rms_rt_srp")
    gptp.plot_all("5.16.2-rt19, rt+SRP, noise", rms_only=True)
