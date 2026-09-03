/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/elm327_channel.h"

#include <cctype>
#include <cstdio>

namespace cansdk
{

namespace
{

constexpr char kPrompt = '>';

std::string trim(const std::string& s)
{
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) != 0))
    {
        ++a;
    }
    while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) != 0))
    {
        --b;
    }
    return s.substr(a, b - a);
}

std::string to_upper(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse_hex_u32(const std::string& tok, uint32_t& out)
{
    if (tok.empty() || tok.size() > 8)
    {
        return false;
    }
    uint32_t v = 0;
    for (char c : tok)
    {
        const int n = hex_nibble(c);
        if (n < 0)
        {
            return false;
        }
        v = (v << 4) | static_cast<uint32_t>(n);
    }
    out = v;
    return true;
}

std::vector<std::string> split_ws(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (std::isspace(static_cast<unsigned char>(c)) != 0)
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        out.push_back(cur);
    }
    return out;
}

std::string hex_byte(uint8_t v)
{
    char b[3];
    std::snprintf(b, sizeof b, "%02X", v);
    return std::string(b);
}

std::string hex_id(uint32_t v, bool extended)
{
    char b[16];
    std::snprintf(b, sizeof b, extended ? "%08X" : "%03X", v);
    return std::string(b);
}

} // namespace

Elm327Channel::Elm327Channel(IByteStream& stream, Elm327Config config) : stream_(stream), cfg_(config)
{
}

bool Elm327Channel::is_status_line(const std::string& line)
{
    const std::string u = to_upper(trim(line));
    if (u.empty())
    {
        return true;
    }

    if (u == "?" || u == "OK" || u == "STOPPED" || u == "SEARCHING...")
    {
        return true;
    }
    static const char* kWords[] = {"NO DATA",   "CAN ERROR",  "BUFFER FULL",       "BUS BUSY",
                                   "BUS ERROR", "DATA ERROR", "UNABLE TO CONNECT", "BUS INIT",
                                   "ERR",       "LV RESET",   "FB ERROR",          "ELM327"};
    for (const char* w : kWords)
    {
        if (u.find(w) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

Result<int> Elm327Channel::protocol_number(uint32_t bitrate, bool extended_ids)
{
    if (bitrate == 500000)
    {
        return extended_ids ? 7 : 6;
    }
    if (bitrate == 250000)
    {
        return extended_ids ? 9 : 8;
    }
    return Result<int>::error("ELM327 supports 500000 or 250000 bit/s for ISO 15765-4; got " + std::to_string(bitrate));
}

Result<CanFrame> Elm327Channel::parse_frame_line(const std::string& line)
{
    const std::string t = trim(line);
    if (is_status_line(t))
    {
        return Result<CanFrame>::error("ELM status line: " + t);
    }

    std::vector<std::string> tok = split_ws(t);
    if (tok.size() < 2)
    {
        return Result<CanFrame>::error("ELM line too short to be a frame: " + t);
    }

    uint32_t id = 0;
    if (!parse_hex_u32(tok[0], id))
    {
        return Result<CanFrame>::error("ELM line has non-hex ID: " + t);
    }

    CanFrame f{};
    f.arbitration_id = id;
    f.type = (tok[0].size() > 3 || id > kMaxStandardId) ? FrameType::Extended : FrameType::Standard;

    std::size_t n = 0;
    for (std::size_t i = 1; i < tok.size() && n < 8; ++i)
    {
        if (tok[i].size() != 2)
        {
            return Result<CanFrame>::error("ELM data token is not a byte: " + tok[i]);
        }
        uint32_t b = 0;
        if (!parse_hex_u32(tok[i], b))
        {
            return Result<CanFrame>::error("ELM data token is not hex: " + tok[i]);
        }
        f.data[n++] = static_cast<uint8_t>(b);
    }
    f.dlc = static_cast<uint8_t>(n);
    return f;
}

Result<void> Elm327Channel::send_raw(const std::string& text)
{
    const auto* p = reinterpret_cast<const uint8_t*>(text.data());
    std::size_t remaining = text.size();
    while (remaining > 0)
    {
        auto w = stream_.write(p, remaining);
        if (!w)
        {
            return Result<void>::error(w.error());
        }
        if (*w == 0)
        {
            return Result<void>::error("ELM327: byte stream accepted no data");
        }
        p += *w;
        remaining -= *w;
    }
    return {};
}

Result<std::vector<std::string>> Elm327Channel::read_until_prompt(uint32_t timeout_ms)
{
    uint8_t buf[256];
    uint32_t waited = 0;
    constexpr uint32_t kSlice = 50;

    while (rx_accum_.find(kPrompt) == std::string::npos)
    {
        auto r = stream_.read(buf, sizeof buf, kSlice);
        if (!r)
        {
            return Result<std::vector<std::string>>::error(r.error());
        }
        if (*r > 0)
        {
            rx_accum_.append(reinterpret_cast<const char*>(buf), *r);
            continue;
        }
        waited += kSlice;
        if (waited >= timeout_ms)
        {
            return Result<std::vector<std::string>>::error("ELM327: timed out waiting for prompt");
        }
    }

    const std::size_t pos = rx_accum_.find(kPrompt);
    const std::string block = rx_accum_.substr(0, pos);
    rx_accum_.erase(0, pos + 1);

    std::vector<std::string> lines;
    std::string cur;
    for (char c : block)
    {
        if (c == '\r' || c == '\n')
        {
            const std::string t = trim(cur);
            if (!t.empty())
            {
                lines.push_back(t);
            }
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    const std::string t = trim(cur);
    if (!t.empty())
    {
        lines.push_back(t);
    }
    return lines;
}

Result<std::vector<std::string>> Elm327Channel::command(const std::string& cmd, uint32_t timeout_ms)
{
    if (timeout_ms == 0)
    {
        timeout_ms = cfg_.command_timeout_ms;
    }
    if (auto s = send_raw(cmd + "\r"); !s)
    {
        return Result<std::vector<std::string>>::error(s.error());
    }
    auto lines = read_until_prompt(timeout_ms);
    if (!lines)
    {
        return lines;
    }

    std::vector<std::string> out;
    const std::string echo = to_upper(trim(cmd));
    for (const std::string& l : *lines)
    {
        if (to_upper(l) == echo)
        {
            continue;
        }
        out.push_back(l);
    }
    return out;
}

Result<void> Elm327Channel::expect_ok(const std::string& cmd, bool lenient)
{
    auto r = command(cmd);
    if (!r)
    {
        return Result<void>::error(cmd + ": " + r.error());
    }
    for (const std::string& l : *r)
    {
        if (to_upper(l).find("OK") != std::string::npos)
        {
            return {};
        }
    }
    if (lenient)
    {
        return {};
    }
    const std::string got = r->empty() ? std::string("(no reply)") : (*r)[0];
    return Result<void>::error(cmd + ": expected OK, got '" + got + "'");
}

Result<void> Elm327Channel::initialise(uint32_t bitrate, bool extended_ids)
{
    auto proto = protocol_number(bitrate, extended_ids);
    if (!proto)
    {
        return Result<void>::error(proto.error());
    }

    stream_.flush_input();
    rx_accum_.clear();
    rx_queue_.clear();
    header_set_ = false;

    auto z = command("ATZ", cfg_.reset_timeout_ms);
    if (!z)
    {
        return Result<void>::error("ATZ: " + z.error());
    }
    for (const std::string& l : *z)
    {
        if (!l.empty())
        {
            info_.identifier = l;
        }
    }

    if (auto r = expect_ok("ATE0"); !r)
    {
        return r;
    }

    (void)expect_ok("ATL0", true);
    if (auto r = expect_ok("ATS1", true); !r)
    {
        return r;
    }
    if (cfg_.headers_on)
    {
        if (auto r = expect_ok("ATH1"); !r)
        {
            return r;
        }
    }
    if (cfg_.adaptive_timing)
    {
        (void)expect_ok("ATAT1", true);
    }

    if (auto r = expect_ok("ATSP" + std::to_string(*proto)); !r)
    {
        return r;
    }

    if (!cfg_.auto_format)
    {
        if (auto r = expect_ok("ATCAF0"); !r)
        {
            return Result<void>::error(std::string("adapter rejected ATCAF0, so it cannot carry raw "
                                                   "CAN payloads: ") +
                                       r.error());
        }
    }

    open_ = true;
    last_error_.clear();
    return {};
}

void Elm327Channel::open(uint32_t bitrate, BusProtocol protocol)
{
    if (protocol != BusProtocol::CAN)
    {
        last_error_ = "Elm327Channel supports BusProtocol::CAN only";
        open_ = false;
        return;
    }
    if (auto r = initialise(bitrate, false); !r)
    {
        last_error_ = r.error();
        open_ = false;
    }
}

void Elm327Channel::close()
{
    if (open_)
    {
        (void)command("ATPC");
    }
    open_ = false;
    rx_accum_.clear();
    rx_queue_.clear();
    header_set_ = false;
}

bool Elm327Channel::isOpen() const
{
    return open_ && stream_.is_open();
}

void Elm327Channel::write(const CanFrame& frame)
{
    if (!open_)
    {
        last_error_ = "Elm327Channel::write on a closed channel";
        return;
    }

    if (!header_set_ || tx_header_ != frame.arbitration_id)
    {
        const bool ext = frame.type == FrameType::Extended;
        if (auto r = expect_ok("ATSH" + hex_id(frame.arbitration_id, ext)); !r)
        {
            last_error_ = r.error();
            return;
        }
        tx_header_ = frame.arbitration_id;
        header_set_ = true;
    }

    std::string payload;
    for (uint8_t i = 0; i < frame.dlc && i < 8; ++i)
    {
        payload += hex_byte(frame.data[i]);
    }
    if (payload.empty())
    {
        last_error_ = "Elm327Channel::write with empty payload";
        return;
    }

    auto lines = command(payload);
    if (!lines)
    {
        last_error_ = lines.error();
        return;
    }
    for (const std::string& l : *lines)
    {
        auto f = parse_frame_line(l);
        if (f)
        {
            rx_queue_.push_back(*f);
        }
        else if (!is_status_line(l))
        {
            last_error_ = f.error();
        }
    }
}

std::vector<CanFrame> Elm327Channel::read(uint32_t timeout_ms)
{
    std::vector<CanFrame> out;

    while (!rx_queue_.empty())
    {
        out.push_back(rx_queue_.front());
        rx_queue_.pop_front();
    }
    if (!out.empty() || !open_)
    {
        return out;
    }

    auto lines = read_until_prompt(timeout_ms);
    if (!lines)
    {
        return out;
    }
    for (const std::string& l : *lines)
    {
        auto f = parse_frame_line(l);
        if (f)
        {
            out.push_back(*f);
        }
    }
    return out;
}

void Elm327Channel::setFilter(uint32_t mask, uint32_t pattern)
{
    if (!open_)
    {
        return;
    }

    if (mask == kMaxStandardId || mask == 0x1FFFFFFFu || mask == 0xFFFFFFFFu)
    {
        (void)expect_ok("ATCRA" + hex_id(pattern, pattern > kMaxStandardId), true);
        return;
    }
    (void)expect_ok("ATCM" + hex_id(mask, mask > kMaxStandardId), true);
    (void)expect_ok("ATCF" + hex_id(pattern, pattern > kMaxStandardId), true);
}

void Elm327Channel::clearFilters()
{
    if (!open_)
    {
        return;
    }
    (void)expect_ok("ATCRA", true);
}

} // namespace cansdk
