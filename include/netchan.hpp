/*
 * Copyright 2023 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#include <netchan.h>
#include <string>

namespace netchan {

class NetHandler {
public:
    NetHandler(std::string ifname, std::string logfile, bool srp = false) :
        logfile(logfile),
        hmap_size(127),
        use_srp(srp),
        valid(true)
    {
        nh = nh_create_init(ifname.c_str(), hmap_size, logfile.length() > 0 ? logfile.c_str() : NULL);
        nh_set_srp(nh, srp);

        if (!nh) {
            fprintf(stderr, "%s() FAILED creating handler\n", __func__);
            valid = false;
        }
    };

    NetHandler(std::string ifname) : NetHandler(ifname, "") {};
    NetHandler(std::string ifname, std::string logfile) : NetHandler(ifname, logfile, false) {};
    NetHandler(std::string ifname, bool use_srp) : NetHandler(ifname, "", use_srp) {};

    ~NetHandler()
    {
        if (nh)
            nh_destroy(&nh);
    };

    bool set_tx_prio(int tx_prio)
    {
        return nh_set_tx_prio(nh, tx_prio);
    }

    struct channel * new_tx_channel(struct channel_attrs *attrs)
    {
        if (!valid)
            return NULL;
        return chan_create_tx(nh, attrs, true);
    }

    struct channel * new_rx_channel(struct channel_attrs *attrs)
    {
        if (!valid)
            return NULL;
        return chan_create_rx(nh, attrs, true);
    }

    int active_tx(void) { return nh_get_num_tx(nh); }
    int active_rx(void) { return nh_get_num_rx(nh); }

    void verbose(void) { nh_set_verbose(nh, true); }

private:
    struct nethandler *nh;
    int hmap_size = 127;
    std::string logfile;
    bool use_srp;
    bool valid;
};

/**
 * Extremely simple C++ wrapper to recreate C-macro behavior
 */
class NetChan {
public:
    void stop(void) {};
    bool ready(void) {
        return chan_ready(ch);
    }

    // Wait for 1 second before timing out
    bool ready_wait_once(void) {
        return chan_ready_timedwait(ch, 100000000) == 0;
    }

    void ready_wait(void) {
        int ctr = 0;
        do {
            if (ch->nh->verbose)
                std::cout << ctr++ << " ...waiting..." << std::endl;
        } while (!ready_wait_once());
    }

protected:
    bool use_srp;
    struct channel *ch;
};


class NetChanTx : public NetChan {
public:
    NetChanTx(class NetHandler& nh,
              struct channel_attrs *attrs)
    {
        ch = nh.new_tx_channel(attrs);
    }

    bool send(void *data) {
        if (!ch)
            return false;
        wait_for_tx_slot(ch);
        return chan_send_now(ch, data) == ch->payload_size;
    }

    bool send_wait(void *data) {
        if (!ch)
            return false;

        return chan_send_now_wait(ch, data) == ch->payload_size;
    }
};

class NetChanRx : public NetChan {
public:
    NetChanRx(class NetHandler& nh,
              struct channel_attrs *attrs)
    {
        ch = nh.new_rx_channel(attrs);
    }

    bool read(void *data) {
        if (!ch)
            return false;

        return chan_read(ch, data) > 0;
    }

    bool read_wait(void *data) {
        if (!ch)
            return false;

        return chan_read_wait(ch, data) > 0;
    }

    void stop(void) {
        chan_destroy(&ch);
        ch = NULL;
    }
};
} //  namespace netchan

