/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file frame_trace.h
/// Session tracing — capture every CAN frame in and out so a failed exchange
/// can be analysed after the fact.
///
/// When probing an unknown ECU blind, the single most valuable artifact is the
/// raw wire log: every request, every response, and crucially every frame the
/// higher layers *could not* parse. TracingChannel wraps any IChannel and hands
/// each frame to a sink before passing it through, so the trace sits below the
/// ISO-TP and UDS layers and sees exactly what crossed the bus, malformed
/// frames included.
///
/// The sink interface is portable (no file I/O in the SDK core); TextFrameLog is
/// a ready-made sink that writes a human-readable, greppable log to a path the
/// caller supplies.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "cansdk/can_frame.h"
#include "cansdk/channel.h"

namespace cansdk
{

/// Direction of a traced frame, from the tester's point of view.
enum class FrameDir : uint8_t
{
    Tx, ///< Written by us to the ECU.
    Rx, ///< Received from the ECU.
};

/// One traced frame with a monotonic microsecond timestamp.
struct TracedFrame
{
    FrameDir dir;
    uint64_t t_us;
    CanFrame frame;
};

/// Receives traced frames. Implementations must not throw.
class IFrameSink
{
public:
    virtual ~IFrameSink() = default;
    virtual void on_frame(const TracedFrame& f) = 0;
    /// Optional free-text annotation interleaved with the frames (e.g. an
    /// operation label, or a decoded error), so the log reads as a narrative.
    virtual void on_note(const std::string& /*text*/)
    {
    }
};

/// IChannel decorator that traces every frame to a sink, then forwards.
class TracingChannel : public IChannel
{
public:
    TracingChannel(IChannel& inner, IFrameSink& sink) : inner_(inner), sink_(sink)
    {
    }

    void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) override
    {
        inner_.open(bitrate, protocol);
    }
    void close() override
    {
        inner_.close();
    }
    [[nodiscard]] bool isOpen() const override
    {
        return inner_.isOpen();
    }
    void setFilter(uint32_t mask, uint32_t pattern) override
    {
        inner_.setFilter(mask, pattern);
    }
    void clearFilters() override
    {
        inner_.clearFilters();
    }
    void flushTxBuffer() override
    {
        inner_.flushTxBuffer();
    }

    void write(const CanFrame& frame) override;
    [[nodiscard]] std::vector<CanFrame> read(uint32_t timeout_ms) override;

private:
    IChannel& inner_;
    IFrameSink& sink_;
};

/// A frame sink that appends a human-readable log line per frame.
///
/// Format (one line per frame):
///   `<t_us>  TX 7E0  02 3E 00`
///   `<t_us>  RX 7E8  03 7F 27 33`
/// Notes are written as `# <text>`. The stream is flushed per line so a crash
/// mid-session still leaves a complete log up to the failure.
class TextFrameLog : public IFrameSink
{
public:
    /// Open `path` for append. Check good() before relying on it.
    explicit TextFrameLog(const std::string& path);

    [[nodiscard]] bool good() const
    {
        return out_.good();
    }

    void on_frame(const TracedFrame& f) override;
    void on_note(const std::string& text) override;

private:
    std::ofstream out_;
};

} // namespace cansdk
