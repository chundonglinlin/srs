//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtsp.hpp>

using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_app_utility.hpp>

SrsRtspConn::SrsRtspConn(ISrsProtocolReadWriter* skt, std::string cip, int port, ISrsResourceManager* cm)
{
    manager_ = cm;
    remote_ip_ = cip;
    remote_port_ = port;
    skt_ = skt;
    rtsp_ = new SrsRtspStack(skt);

    session_id_ = "";

    trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());
}

SrsRtspConn::~SrsRtspConn()
{
    trd_->interrupt();

    srs_freep(trd_);
    srs_freep(skt_);
    srs_freep(rtsp_);
}

void SrsRtspConn::on_before_dispose(ISrsResource* c)
{
    if (disposing_) {
        return;
    }
}

void SrsRtspConn::on_disposing(ISrsResource* c)
{
    if (disposing_) {
        return;
    }
}

string SrsRtspConn::desc()
{
    return "rtsp";
}

const SrsContextId& SrsRtspConn::get_id()
{
    return trd_->cid();
}

string SrsRtspConn::remote_ip()
{
    return "rtsp";
}

srs_error_t SrsRtspConn::start()
{
    return trd_->start();
}

srs_error_t SrsRtspConn::cycle()
{
    srs_error_t err = do_cycle();

    // TODO: FIXME: Should manage RTC TCP connection by _srs_rtc_manager.
    // Because we use manager to manage this object, not the http connection object, so we must remove it here.
    manager_->remove(this);

    // For HTTP-API timeout, we think it's done successfully,
    // because there may be no request or response for HTTP-API.
    if (srs_error_code(err) == ERROR_SOCKET_TIMEOUT) {
        srs_freep(err);
        return srs_success;
    }

    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);

    return srs_success;
}

srs_error_t SrsRtspConn::do_cycle()
{
    srs_error_t err = srs_success;

    srs_trace("rtsp: serve %s:%d", remote_ip_.c_str(), remote_port_);
    
    // consume all rtsp messages.
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }

        SrsRtspRequest* req = NULL;
        if ((err = rtsp_->recv_message(&req)) != srs_success) {
            srs_trace("rtsp recv_message failed, err=%s", srs_error_desc(err).c_str());

            return srs_error_wrap(err, "recv message");
        }
        SrsAutoFree(SrsRtspRequest, req);

        // create session.
        if (session_id_.empty()) {
            session_id_ = srs_random_str(8);
        }

        srs_trace("rtsp: got rtsp request: method=%s, uri=%s, CSeq:%d", req->method.c_str(), req->uri.c_str(), (int)req->seq);

        if (req->is_options()) {
            /**
             * OPTIONS:
             * OPTIONS rtsp://10.0.16.111:554/Streaming/Channels/101 RTSP/1.0
             * CSeq: 1
             * User-Agent: Lavf59.26.100

             * RTSP/1.0 200 OK
             * CSeq: 1
             * Public: OPTIONS, DESCRIBE, PLAY, PAUSE, SETUP, TEARDOWN, SET_PARAMETER, GET_PARAMETER
             * Date: Fri, Dec 01 2023 11:15:59 GMT
             */

            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
            /**
             * DESCRIBE:
             * DESCRIBE rtsp://10.0.16.111:554/Streaming/Channels/101 RTSP/1.0
             * Accept: application/sdp
             * CSeq: 3
             * User-Agent: Lavf59.26.100
             * Authorization: Digest username="admin", realm="IP Camera(11266)", nonce="f6995a36c2ed0cccfb4e6c2a33a882c9", uri="rtsp://10.0.16.111:554/Streaming/Channels/101", response="9858265ba07711a41cb2ab44c9547c98"

             * RTSP/1.0 200 OK
             * CSeq: 3
             * Content-Type: application/sdp
             * Content-Base: rtsp://10.0.16.111:554/Streaming/Channels/101/
             * Content-Length: 894
             * SDP
             */

            // rtsp description.
            SrsRtspDescribeResponse *res = new SrsRtspDescribeResponse((int)req->seq);
            res->session = session_id_;
            res->content_base = req->uri;

            SrsRtspSdp* sdp = new SrsRtspSdp();
            /*
            res->sdp->video_stream_id = video_id;
            res->sdp->video_codec = video_codec;
            res->sdp->video_sps = h264_sps;
            res->sdp->video_pps = h264_pps;
            res->sdp->video_sample_rate = 90000;
            res->sdp->video_channel = 0;
            res->sdp->video_payload_type = 96;
            res->sdp->video_protocol = "RTP/AVP";
            res->sdp->video_transport_format = "RAW/RAW/UDP";
            res->sdp->audio_stream_id = audio_id;
            res->sdp->audio_codec = audio_codec;
            res->sdp->audio_sh = aac_specific_config;
            res->sdp->audio_sample_rate = audio_sample_rate;
            res->sdp->audio_channel = audio_channel;
            res->sdp->audio_payload_type = 97;
            res->sdp->audio_protocol = "RTP/AVP";
            res->sdp->audio_transport_format = "RAW/RAW/UDP";
            */
            res->sdp = sdp;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response describe");
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);

            int lpm = 0;
            /*
            if ((err = rtsp_->alloc_port(&lpm)) != srs_success) {
                return srs_error_wrap(err, "alloc port");
            }
            SrsRtpConn* rtp = NULL;
            if (req->stream_id == video_id) {
                srs_freep(video_rtp);
                rtp = video_rtp = new SrsRtpConn(this, lpm, video_id);
            } else {
                srs_freep(audio_rtp);
                rtp = audio_rtp = new SrsRtpConn(this, lpm, audio_id);
            }
            if ((err = rtp->listen()) != srs_success) {
                return srs_error_wrap(err, "rtp listen");
            }
            */

            int video_id = 0;
            srs_trace("rtsp: #%d %s over %s/%s/%s %s client-port=%d-%d, server-port=%d-%d",
                req->stream_id, (req->stream_id == video_id)? "Video":"Audio",
                req->transport->transport.c_str(), req->transport->profile.c_str(), req->transport->lower_transport.c_str(),
                req->transport->cast_type.c_str(), req->transport->client_port_min, req->transport->client_port_max,
                lpm, lpm + 1);

            // setup response.
            SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = lpm;
            res->local_port_max = lpm + 1;
            res->video_ssrc = srs_random_str(8);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }
        } else if (req->is_announce()) {
            // PUBLISH
            srs_warn("rtsp: publish not support yet");
        } else if (req->is_play()) {
            SrsRtspPlayResponse* res = new SrsRtspPlayResponse((int)req->seq);
            res->session = session_id_;
            res->content_base = req->uri;

            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }
        } else if (req->is_record()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }
        } else if (req->is_teardown()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }
        } else {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response default");
            }
        }
    }
    
    return err;
}
