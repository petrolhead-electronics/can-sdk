/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file isotp.h
/// ISO 15765-2 (ISO-TP) transport over an IChannel.
///
/// Reassembles/segments diagnostic messages so higher layers (UDS/KWP) can work
/// in whole PDUs. Works with any transport CAN ID pair — standard OBD
/// (0x7E0/0x7E8) or Ducati's per-ECU pairs (0x7E2/0x7EA, 0x7E3/0x7EB, …).
///
/// This layer only frames bytes; it does not transmit unless the caller invokes
/// send()/request(). Callers that must honour passive-mode safety gate at the
/// service layer (see diag_service.h), not here.

#include <cstdint>
#include <deque>
#include <vector>

#include "cansdk/result.h"
#include "cansdk/channel.h"

namespace cansdk
{

struct IsoTpConfig
{
    uint32_t tx_id = 0x7E0;           ///< Request (transmit) CAN ID.
    uint32_t rx_id = 0x7E8;           ///< Response (receive) CAN ID.
    bool extended_id = false;         ///< Use 29-bit addressing.
    bool use_padding = true;          ///< Pad frames to 8 bytes.
    uint8_t padding = 0x00;           ///< Padding byte (MelcoDiag/OBD often 0x00/0x55).
    uint8_t fc_block_size = 0x00;     ///< Flow-control BlockSize (0 = send all).
    uint8_t fc_stmin = 0x00;          ///< Flow-control STmin.
    uint32_t p2_timeout_ms = 1000;    ///< Per-frame response timeout.
    uint32_t total_timeout_ms = 3000; ///< Whole-message assembly timeout.
    uint8_t channel_id = 0;           ///< Logical channel to stamp on TX frames.
    /// How many `7F xx 78` (responsePending) frames a single request tolerates
    /// before giving up. The default suits normal diagnostics; a flash loader
    /// that answers "busy" once per erase block needs this raised (our NC write
    /// SBL emits one 78 per erase block — 14 — before the final positive).
    uint8_t max_response_pending = 8;
};

/// Segmenting/reassembling ISO-TP transport bound to one IChannel + ID pair.
class IsoTpTransport
{
public:
    IsoTpTransport(IChannel& channel, IsoTpConfig config) : channel_(channel), cfg_(std::move(config))
    {
    }

    const IsoTpConfig& config() const
    {
        return cfg_;
    }
    void set_config(IsoTpConfig config)
    {
        cfg_ = std::move(config);
    }

    /// Segment and transmit a full PDU (adds SF/FF/CF framing + flow control).
    [[nodiscard]] Result<void> send(const std::vector<uint8_t>& data);

    /// Receive and reassemble one full PDU addressed to rx_id.
    [[nodiscard]] Result<std::vector<uint8_t>> recv(uint32_t timeout_ms);

    /// send() followed by recv() using the configured total timeout.
    [[nodiscard]] Result<std::vector<uint8_t>> request(const std::vector<uint8_t>& data);

private:
    [[nodiscard]] Result<void> write_frame(const std::vector<uint8_t>& payload);
    [[nodiscard]] Result<CanFrame> read_matching(uint32_t timeout_ms);
    [[nodiscard]] Result<void> send_flow_control();

    IChannel& channel_;
    IsoTpConfig cfg_;
    std::deque<CanFrame> rx_buf_; ///< Frames read from the channel but not yet consumed.
};

} // namespace cansdk
