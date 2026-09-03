/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/can_frame.h"
#include <cstdio>

namespace cansdk
{

namespace
{

template <typename... Args> std::string fmt(const char* spec, Args... args)
{
    char buf[128];
    const int n = std::snprintf(buf, sizeof buf, spec, args...);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
}

} // namespace

std::string validate_frame(const CanFrame& frame)
{

    if (frame.type == FrameType::Standard && frame.arbitration_id > kMaxStandardId)
    {
        return fmt("Standard frame ID 0x%X exceeds max 0x%X", frame.arbitration_id, kMaxStandardId);
    }
    if (frame.type == FrameType::Extended && frame.arbitration_id > kMaxExtendedId)
    {
        return fmt("Extended frame ID 0x%X exceeds max 0x%X", frame.arbitration_id, kMaxExtendedId);
    }
    if (frame.type == FrameType::J1850 && frame.arbitration_id > kMaxJ1850Id)
    {
        return fmt("J1850 frame ID 0x%X exceeds max 0x%X", frame.arbitration_id, kMaxJ1850Id);
    }

    const uint8_t max_dlc = (frame.type == FrameType::FD) ? kCanFdMaxDlc : kClassicCanMaxDlc;
    if (frame.dlc > max_dlc)
    {
        return fmt("DLC %u exceeds maximum %u for frame type %s", static_cast<unsigned>(frame.dlc),
                   static_cast<unsigned>(max_dlc), frame_type_to_string(frame.type));
    }

    for (uint8_t i = frame.dlc; i < frame.data.size(); ++i)
    {
        if (frame.data[i] != 0)
        {
            return fmt("Non-zero byte at data[%u] beyond DLC %u", static_cast<unsigned>(i),
                       static_cast<unsigned>(frame.dlc));
        }
    }

    return {};
}

bool is_valid_id(uint32_t id, FrameType type)
{
    switch (type)
    {
    case FrameType::Standard:
        return id <= kMaxStandardId;
    case FrameType::Extended:
    case FrameType::FD:
        return id <= kMaxExtendedId;
    case FrameType::Error:
    case FrameType::Remote:
        return id <= kMaxExtendedId;
    case FrameType::J1850:
        return id <= kMaxJ1850Id;
    }
    return false;
}

const char* frame_type_to_string(FrameType type)
{
    switch (type)
    {
    case FrameType::Standard:
        return "Std";
    case FrameType::Extended:
        return "Ext";
    case FrameType::FD:
        return "FD";
    case FrameType::Error:
        return "Err";
    case FrameType::Remote:
        return "Rtr";
    case FrameType::J1850:
        return "J1850";
    }
    return "???";
}

} // namespace cansdk
