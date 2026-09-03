/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file elm327_channel.h
/// Elm327Channel — drives a real ELM327 adapter and presents it as an IChannel.
///
/// Portable: the AT dialogue and response parsing live here, while the raw byte
/// pipe (serial / BLE / USB) is supplied by an IByteStream the platform
/// implements. The same code therefore serves PC and mobile.
///
/// ## Layering: why auto-formatting is turned OFF
///
/// An ELM327 can assemble ISO-TP itself (`ATCAF1`), but this SDK does its own
/// framing in IsoTpTransport. Letting both do it would mean two segmenters
/// disagreeing about flow control. So the channel configures `ATCAF0` and moves
/// raw 8-byte CAN payloads in both directions, which is exactly what IChannel
/// is defined to carry. Flow-control frames are then just ordinary writes
/// produced by IsoTpTransport.
///
/// `ATH1` (headers on) is required so responses carry the arbitration ID and
/// can be turned back into a CanFrame.
///
/// ## Impedance mismatch with IChannel
///
/// IChannel models a free-running bus. An ELM327 is request/response: outside
/// monitor mode it emits nothing until asked. This channel therefore collects
/// the adapter's reply during write() and hands it to the following read().
/// For diagnostics — request, then response — this is the natural shape, and it
/// is what IsoTpTransport does. It is not a general bus sniffer; see
/// monitor_all() for that.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "cansdk/byte_stream.h"
#include "cansdk/can_frame.h"
#include "cansdk/result.h"
#include "cansdk/channel.h"

namespace cansdk
{

struct Elm327Config
{
    uint32_t command_timeout_ms = 5000; ///< Max wait for the '>' prompt.
    uint32_t reset_timeout_ms = 8000;   ///< ATZ can be slow on clones.
    bool headers_on = true;             ///< ATH1 — required to recover the CAN ID.
    bool auto_format = false;           ///< ATCAF0 — see the note above.
    bool adaptive_timing = true;        ///< ATAT1.
};

/// Identifying information reported by the adapter (from the ATZ banner / ATI).
struct Elm327Info
{
    std::string identifier; ///< e.g. "ELM327 v1.5", or a clone's banner.
};

class Elm327Channel : public IChannel
{
public:
    /// @param stream Byte pipe to the adapter. Must outlive this channel.
    explicit Elm327Channel(IByteStream& stream, Elm327Config config = {});

    // --- IChannel ---------------------------------------------------------
    /// Run the initialisation dialogue and select the CAN protocol.
    /// @param bitrate 500000 or 250000 — mapped to an ELM protocol number
    ///                together with `protocol`'s addressing width.
    /// Throws nothing; failures surface via last_error() and isOpen() == false.
    void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) override;

    void close() override;
    [[nodiscard]] std::vector<CanFrame> read(uint32_t timeout_ms) override;
    void write(const CanFrame& frame) override;
    void setFilter(uint32_t mask, uint32_t pattern) override;
    void clearFilters() override;
    [[nodiscard]] bool isOpen() const override;

    // --- ELM-specific -----------------------------------------------------
    /// Fallible variant of open(), preferred inside the SDK.
    [[nodiscard]] Result<void> initialise(uint32_t bitrate, bool extended_ids = false);

    /// Send one AT/OBD command and return the adapter's reply lines
    /// (echo removed, prompt consumed).
    [[nodiscard]] Result<std::vector<std::string>> command(const std::string& cmd, uint32_t timeout_ms = 0);

    [[nodiscard]] const Elm327Info& info() const
    {
        return info_;
    }
    [[nodiscard]] const std::string& last_error() const
    {
        return last_error_;
    }

    /// Map a bitrate + addressing width to an ELM327 `ATSP` protocol number.
    /// 11-bit/500k = 6, 29-bit/500k = 7, 11-bit/250k = 8, 29-bit/250k = 9.
    [[nodiscard]] static Result<int> protocol_number(uint32_t bitrate, bool extended_ids);

    /// Parse one ELM response line ("7E8 03 7F 27 33") into a CanFrame.
    /// Returns an error for status lines such as "NO DATA" or "CAN ERROR".
    [[nodiscard]] static Result<CanFrame> parse_frame_line(const std::string& line);

    /// True if the line is an adapter status/error word rather than data.
    [[nodiscard]] static bool is_status_line(const std::string& line);

private:
    [[nodiscard]] Result<void> send_raw(const std::string& text);
    /// Read until the '>' prompt, splitting into trimmed non-empty lines.
    [[nodiscard]] Result<std::vector<std::string>> read_until_prompt(uint32_t timeout_ms);
    /// Send a command and require "OK" (or any non-error reply when lenient).
    [[nodiscard]] Result<void> expect_ok(const std::string& cmd, bool lenient = false);

    IByteStream& stream_;
    Elm327Config cfg_;
    Elm327Info info_;
    std::string last_error_;
    std::string rx_accum_;          ///< Bytes read but not yet forming a full reply.
    std::deque<CanFrame> rx_queue_; ///< Frames collected during write(), drained by read().
    uint32_t tx_header_ = 0;        ///< Currently programmed ATSH value.
    bool header_set_ = false;
    bool open_ = false;
};

} // namespace cansdk
