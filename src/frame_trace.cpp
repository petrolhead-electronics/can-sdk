/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/frame_trace.h"

#include <cstdio>

#include "cansdk/timestamp.h"

namespace cansdk
{

void TracingChannel::write(const CanFrame& frame)
{
    sink_.on_frame({FrameDir::Tx, host_timestamp_us(), frame});
    inner_.write(frame);
}

std::vector<CanFrame> TracingChannel::read(uint32_t timeout_ms)
{
    std::vector<CanFrame> frames = inner_.read(timeout_ms);
    for (const CanFrame& f : frames)
    {
        sink_.on_frame({FrameDir::Rx, host_timestamp_us(), f});
    }
    return frames;
}

TextFrameLog::TextFrameLog(const std::string& path) : out_(path, std::ios::app)
{
}

void TextFrameLog::on_frame(const TracedFrame& f)
{
    char line[128];

    const char* dir = f.dir == FrameDir::Tx ? "TX" : "RX";
    const bool ext = f.frame.type == FrameType::Extended;
    int n = std::snprintf(line, sizeof line, "%llu  %s %0*X ", static_cast<unsigned long long>(f.t_us), dir,
                          ext ? 8 : 3, f.frame.arbitration_id);
    out_.write(line, n);
    for (uint8_t i = 0; i < f.frame.dlc && i < 8; ++i)
    {
        std::snprintf(line, sizeof line, " %02X", f.frame.data[i]);
        out_.write(line, 3);
    }
    out_.put('\n');
    out_.flush();
}

void TextFrameLog::on_note(const std::string& text)
{
    out_ << "# " << text << "\n";
    out_.flush();
}

} // namespace cansdk
