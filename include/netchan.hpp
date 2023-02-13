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

/**
 * Extremely simple C++ wrapper to recreate C-macro behavior
 */
class NetChan {
public:

    NetChan(std::string name,
            struct net_fifo *nfc,
            int sz,
            bool tx,
            std::string nic = "lo",
            bool use_srp = true) :
        channel_name(name),
        nfc_sz(sz),
        tx(tx)
    {
        set_nic(nic);
        net_fifo_chans = nfc;
        nf_use_srp();
        connect();
    }

    ~NetChan()
    {
        nh_destroy_standalone();
    }

    void verbose() { nf_verbose(); };

protected:
    void set_nic(std::string nic = "lo") { nf_set_nic((char *)nic.c_str()); };
    bool connect() {
        _ch = pdu_create_standalone((char *)channel_name.c_str(), tx, net_fifo_chans, nfc_sz);
        if (_ch)
            return true;
        printf("failed creating stdandalone pdu\n");
        return false;
    }

    struct netchan_avtp *_ch;
    std::string channel_name;
    struct net_fifo *net_fifo_chans;
    int nfc_sz;
    bool tx;
};

class NetChanTx : public NetChan {
public:
    NetChanTx(std::string name,
              struct net_fifo *nfc,
              int sz,
              std::string nic) :
        NetChan(name, nfc, sz, true, nic) {};

    bool do_write(void *data) {
        if (!_ch) {
            printf("_ch not set\n");
            return false;
        }
        return pdu_send_now(_ch, data);
    }

    bool write_wait(void *data) {
        if (!_ch)
            return false;
        return pdu_send_now_wait(_ch, data);
    }
};

class NetChanRx : public NetChan {
public:
    NetChanRx(std::string name,
              struct net_fifo *nfc,
              int sz,
              std::string nic) :
        NetChan(name, nfc, sz, false, nic) {};

    bool read(void *data) {
        if (!_ch)
            return false;
        return pdu_read(_ch, data);
    }

    bool read_wait(void *data) {
        if (!_ch)
            return false;
        return pdu_read_wait(_ch, data);
    }
};
