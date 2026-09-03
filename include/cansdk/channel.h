/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file channel.h
/// IChannel interface — abstract CAN channel for read/write/filter operations.

#include <cstdint>
#include <vector>

#include "cansdk/can_frame.h"
#include "cansdk/result.h"

namespace cansdk
{

/// Abstract CAN channel — wraps one J2534 channel or mock channel.
/// Implementations: J2534Channel, MockChannel.
class IChannel
{
public:
    virtual ~IChannel() = default;

    /// Open the channel at the specified bitrate and bus protocol.
    virtual void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) = 0;

    /// Close the channel. Safe to call if already closed.
    virtual void close() = 0;

    /// Read received CAN frames (blocking with timeout).
    /// @param timeout_ms Maximum wait time in milliseconds.
    /// @return Vector of received frames (may be empty on timeout).
    [[nodiscard]] virtual std::vector<CanFrame> read(uint32_t timeout_ms) = 0;

    /// Write a frame to the bus.
    virtual void write(const CanFrame& frame) = 0;

    /// Set a hardware-level CAN filter (mask/pattern).
    virtual void setFilter(uint32_t mask, uint32_t pattern) = 0;

    /// Remove all hardware-level filters.
    virtual void clearFilters() = 0;

    /// Flush the adapter's internal TX buffer (best-effort; no-op if unsupported).
    /// Call this after stopping replay to prevent the adapter from retransmitting
    /// the last written frame from its internal FIFO.
    virtual void flushTxBuffer()
    {
    }

    /// Check whether the channel is currently open.
    [[nodiscard]] virtual bool isOpen() const = 0;

    // --- Optional native ISO-TP (ISO 15765-2) --------------------------------
    // When the adapter is opened in BusProtocol::ISO15765, it performs the
    // SF/FF/CF framing and flow control itself and paces frames at hardware
    // speed. A flash-loader write done as software ISO-TP — where every
    // consecutive frame is a separate round trip through the 64-bit→32-bit
    // J2534 bridge — is too slow and jittery, and the ECU rejects the block
    // that triggers the erase (NRC 0x72). Handing the adapter one whole PDU per
    // message is one bridge round trip and lets it segment at line rate, exactly
    // as romdrop does. Channels that cannot do this leave the defaults, and the
    // caller falls back to software ISO-TP.

    /// True when this channel can send/receive whole ISO-TP PDUs natively.
    [[nodiscard]] virtual bool supportsHwIsoTp() const
    {
        return false;
    }

    /// Send one full request PDU to `tx_id`; the adapter segments it. `rx_id` is
    /// the ECU's response id, so the adapter's flow-control filter can be set up
    /// for the pair on first use.
    [[nodiscard]] virtual Result<void> sendHwIsoTp(uint32_t /*tx_id*/, uint32_t /*rx_id*/,
                                                   const std::vector<uint8_t>& /*pdu*/)
    {
        return Result<void>::error("channel has no native ISO-TP");
    }

    /// Receive one reassembled response PDU (adapter handles flow control).
    [[nodiscard]] virtual Result<std::vector<uint8_t>> recvHwIsoTp(uint32_t /*timeout_ms*/)
    {
        return Result<std::vector<uint8_t>>::error("channel has no native ISO-TP");
    }
};

} // namespace cansdk
