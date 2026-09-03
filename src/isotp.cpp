/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/isotp.h"

#include <chrono>
#include <cstdio>

namespace cansdk
{

namespace
{
using Clock = std::chrono::steady_clock;

uint64_t elapsed_ms(Clock::time_point start)
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}
} // namespace

Result<void> IsoTpTransport::write_frame(const std::vector<uint8_t>& payload)
{
    CanFrame f{};
    f.arbitration_id = cfg_.tx_id;
    f.type = cfg_.extended_id ? FrameType::Extended : FrameType::Standard;
    f.channel_id = cfg_.channel_id;

    std::size_t n = payload.size();
    if (n > 8)
    {
        n = 8;
    }
    for (std::size_t i = 0; i < n; ++i)
    {
        f.data[i] = payload[i];
    }

    if (cfg_.use_padding)
    {
        for (std::size_t i = n; i < 8; ++i)
        {
            f.data[i] = cfg_.padding;
        }
        f.dlc = 8;
    }
    else
    {
        f.dlc = static_cast<uint8_t>(n);
    }

    channel_.write(f);
    return {};
}

Result<CanFrame> IsoTpTransport::read_matching(uint32_t timeout_ms)
{
    auto start = Clock::now();
    do
    {

        while (!rx_buf_.empty())
        {
            CanFrame f = rx_buf_.front();
            rx_buf_.pop_front();
            if (f.arbitration_id == cfg_.rx_id)
            {
                return f;
            }
        }

        uint64_t spent = elapsed_ms(start);
        uint32_t remaining = spent >= timeout_ms ? 0 : static_cast<uint32_t>(timeout_ms - spent);

        std::vector<CanFrame> frames = channel_.read(remaining == 0 ? 1 : remaining);
        for (const CanFrame& f : frames)
        {
            rx_buf_.push_back(f);
        }
    } while (elapsed_ms(start) < timeout_ms || !rx_buf_.empty());

    return Result<CanFrame>::error("ISO-TP: no response from 0x" +
                                   [](uint32_t v)
                                   {
                                       char b[16];
                                       std::snprintf(b, sizeof b, "%X", v);
                                       return std::string(b);
                                   }(cfg_.rx_id));
}

Result<void> IsoTpTransport::send_flow_control()
{

    return write_frame({0x30, cfg_.fc_block_size, cfg_.fc_stmin});
}

Result<void> IsoTpTransport::send(const std::vector<uint8_t>& data)
{

    if (channel_.supportsHwIsoTp())
    {
        return channel_.sendHwIsoTp(cfg_.tx_id, cfg_.rx_id, data);
    }
    if (data.empty())
    {
        return Result<void>::error("ISO-TP: empty PDU");
    }

    if (data.size() <= 7)
    {

        std::vector<uint8_t> frame;
        frame.reserve(1 + data.size());
        frame.push_back(static_cast<uint8_t>(data.size() & 0x0F));
        frame.insert(frame.end(), data.begin(), data.end());
        return write_frame(frame);
    }

    if (data.size() > 4095)
    {
        return Result<void>::error("ISO-TP: PDU exceeds 4095 bytes");
    }

    std::vector<uint8_t> ff;
    ff.push_back(static_cast<uint8_t>(0x10 | ((data.size() >> 8) & 0x0F)));
    ff.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    std::size_t idx = 0;
    for (; idx < 6; ++idx)
    {
        ff.push_back(data[idx]);
    }
    if (auto r = write_frame(ff); !r)
    {
        return r;
    }

    auto fc = read_matching(cfg_.p2_timeout_ms);
    if (!fc)
    {
        return Result<void>::error("ISO-TP: no flow control — " + fc.error());
    }
    if ((fc->data[0] & 0xF0) != 0x30)
    {
        return Result<void>::error("ISO-TP: expected flow control, got other frame");
    }
    if ((fc->data[0] & 0x0F) == 0x02)
    {
        return Result<void>::error("ISO-TP: flow control overflow/abort");
    }

    uint8_t sn = 1;
    while (idx < data.size())
    {
        std::vector<uint8_t> cf;
        cf.push_back(static_cast<uint8_t>(0x20 | (sn & 0x0F)));
        for (std::size_t k = 0; k < 7 && idx < data.size(); ++k, ++idx)
        {
            cf.push_back(data[idx]);
        }
        if (auto r = write_frame(cf); !r)
        {
            return r;
        }
        sn = static_cast<uint8_t>((sn + 1) & 0x0F);
    }
    return {};
}

Result<std::vector<uint8_t>> IsoTpTransport::recv(uint32_t timeout_ms)
{

    if (channel_.supportsHwIsoTp())
    {
        return channel_.recvHwIsoTp(timeout_ms);
    }
    auto start = Clock::now();
    auto first = read_matching(timeout_ms);
    if (!first)
    {
        return Result<std::vector<uint8_t>>::error(first.error());
    }

    const CanFrame& f = *first;
    uint8_t pci = f.data[0] & 0xF0;

    if (pci == 0x00)
    {

        uint8_t len = f.data[0] & 0x0F;
        if (len == 0 || len > 7)
        {
            return Result<std::vector<uint8_t>>::error("ISO-TP: bad SF length");
        }
        return std::vector<uint8_t>(f.data.begin() + 1, f.data.begin() + 1 + len);
    }

    if (pci == 0x10)
    {

        std::size_t total = (static_cast<std::size_t>(f.data[0] & 0x0F) << 8) | f.data[1];
        if (total < 8 || total > 4095)
        {
            return Result<std::vector<uint8_t>>::error("ISO-TP: bad FF length");
        }
        std::vector<uint8_t> out(f.data.begin() + 2, f.data.begin() + 8);

        if (auto r = send_flow_control(); !r)
        {
            return Result<std::vector<uint8_t>>::error(r.error());
        }

        uint8_t expected_sn = 1;
        while (out.size() < total)
        {
            uint64_t spent = elapsed_ms(start);
            if (spent >= timeout_ms)
            {
                return Result<std::vector<uint8_t>>::error("ISO-TP: timeout reassembling multi-frame response");
            }
            auto cf = read_matching(static_cast<uint32_t>(timeout_ms - spent));
            if (!cf)
            {
                return Result<std::vector<uint8_t>>::error(cf.error());
            }
            if ((cf->data[0] & 0xF0) != 0x20)
            {
                continue;
            }
            if ((cf->data[0] & 0x0F) != (expected_sn & 0x0F))
            {
                return Result<std::vector<uint8_t>>::error("ISO-TP: consecutive-frame sequence error");
            }
            for (std::size_t k = 1; k < 8 && out.size() < total; ++k)
            {
                out.push_back(cf->data[k]);
            }
            expected_sn = static_cast<uint8_t>((expected_sn + 1) & 0x0F);
        }
        return out;
    }

    return Result<std::vector<uint8_t>>::error("ISO-TP: unexpected PCI in response");
}

Result<std::vector<uint8_t>> IsoTpTransport::request(const std::vector<uint8_t>& data)
{
    if (auto r = send(data); !r)
    {
        return Result<std::vector<uint8_t>>::error(r.error());
    }
    return recv(cfg_.total_timeout_ms);
}

} // namespace cansdk
