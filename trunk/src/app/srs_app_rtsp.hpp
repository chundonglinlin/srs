//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_HPP
#define SRS_APP_RTSP_HPP

#include <srs_core.hpp>

#include <string>
#include <map>

#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtsp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_io.hpp>

class ISrsResourceManager;
class SrsCoroutine;
class SrsTcpConnection;
class ISrsKbpsDelta;
class SrsErrorPithyPrint;
class ISrsRtcTransport;
class SrsRtspConn;

// For Rtsp Server over TCP.
class SrsRtspConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler
{
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager_;
    // Use a coroutine to serve the TCP connection.
    SrsCoroutine* trd_;
    // The ip and port of client.
    std::string remote_ip_;
    int remote_port_;
    // rtsp session object.
    SrsRtspStack* rtsp_;
    ISrsProtocolReadWriter* skt_;
    std::string session_id_;
public:
    bool disposing_;
public:
    SrsRtspConn(ISrsProtocolReadWriter* skt, std::string cip, int port, ISrsResourceManager* cm);
    virtual ~SrsRtspConn();
    // interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource* c);
    virtual void on_disposing(ISrsResource* c);
public:
    virtual std::string desc();
    virtual const SrsContextId& get_id();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
// Interface ISrsStartable
public:
    virtual srs_error_t start();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
};

#endif
