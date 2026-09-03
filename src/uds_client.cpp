/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/uds_client.h"

#include <cstdio>

namespace cansdk
{

namespace
{
std::string hex2(uint8_t v)
{
    char b[4];
    std::snprintf(b, sizeof b, "%02X", v);
    return std::string(b);
}
} // namespace

const char* nrc_to_string(uint8_t nrc)
{
    switch (nrc)
    {
    case 0x10:
        return "generalReject";
    case 0x11:
        return "serviceNotSupported";
    case 0x12:
        return "subFunctionNotSupported";
    case 0x13:
        return "incorrectMessageLengthOrInvalidFormat";
    case 0x22:
        return "conditionsNotCorrect";
    case 0x24:
        return "requestSequenceError";
    case 0x31:
        return "requestOutOfRange";
    case 0x33:
        return "securityAccessDenied";
    case 0x35:
        return "invalidKey";
    case 0x36:
        return "exceedNumberOfAttempts";
    case 0x37:
        return "requiredTimeDelayNotExpired";
    case 0x78:
        return "responsePending";
    case 0x7E:
        return "subFunctionNotSupportedInActiveSession";
    case 0x7F:
        return "serviceNotSupportedInActiveSession";
    default:
        return "unknownNRC";
    }
}

Result<Bytes> UdsClient::request(uint8_t svc, const Bytes& params)
{
    Bytes pdu;
    pdu.reserve(1 + params.size());
    pdu.push_back(svc);
    pdu.insert(pdu.end(), params.begin(), params.end());

    if (auto s = tp_.send(pdu); !s)
    {
        return Result<Bytes>::error(s.error());
    }

    const int kMaxPending = tp_.config().max_response_pending > 0 ? tp_.config().max_response_pending : 8;
    for (int attempt = 0; attempt <= kMaxPending; ++attempt)
    {
        auto resp = tp_.recv(tp_.config().total_timeout_ms);
        if (!resp)
        {
            return Result<Bytes>::error(resp.error());
        }
        const Bytes& r = *resp;
        if (r.empty())
        {
            return Result<Bytes>::error("empty diagnostic response");
        }

        if (r[0] == kNegativeResponse)
        {
            if (r.size() < 3)
            {
                return Result<Bytes>::error("malformed negative response");
            }
            uint8_t nrc = r[2];
            if (nrc == kNrcResponsePending && attempt < kMaxPending)
            {
                continue;
            }
            std::string msg = "NRC 0x" + hex2(nrc) + " (" + nrc_to_string(nrc) + ") on service 0x" + hex2(svc);

            if (r.size() > 3)
            {
                msg += " [data:";
                for (std::size_t i = 3; i < r.size(); ++i)
                {
                    msg += " " + hex2(r[i]);
                }
                msg += "]";
            }
            return Result<Bytes>::error(msg);
        }

        if (r[0] != static_cast<uint8_t>(svc + kPositiveOffset))
        {
            return Result<Bytes>::error("unexpected response SID 0x" + hex2(r[0]) + " for service 0x" + hex2(svc));
        }
        return Bytes(r.begin() + 1, r.end());
    }
    return Result<Bytes>::error("service 0x" + hex2(svc) + ": too many pending responses");
}

Result<Bytes> UdsClient::diagnostic_session(uint8_t sub)
{
    return request(service::kDiagnosticSessionControl, {sub});
}

Result<void> UdsClient::stop_diagnostic_session()
{
    if (auto r = request(service::kStopDiagnosticSession); !r)
    {
        return Result<void>::error(r.error());
    }
    return {};
}

Result<void> UdsClient::tester_present()
{
    if (auto r = request(service::kTesterPresent, {0x00}); !r)
    {
        return Result<void>::error(r.error());
    }
    return {};
}

Result<Bytes> UdsClient::read_data_by_id(uint16_t did)
{
    return request(service::kReadDataByIdentifier, {static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did & 0xFF)});
}

Result<Bytes> UdsClient::read_ecu_identification(uint8_t id)
{
    return request(service::kReadEcuIdentification, {id});
}

Result<Bytes> UdsClient::read_vin()
{
    auto r = read_data_by_id(0xF190);
    if (!r)
    {
        return Result<Bytes>::error(r.error());
    }

    const Bytes& d = *r;
    std::size_t off = (d.size() >= 2) ? 2 : 0;
    return Bytes(d.begin() + off, d.end());
}

Result<Bytes> UdsClient::read_dtcs_uds(uint8_t status_mask)
{

    return request(service::kReadDtcInformation, {0x02, status_mask});
}

Result<Bytes> UdsClient::read_dtcs_kwp()
{

    return request(service::kReadDtcByStatus, {0x00, 0xFF, 0x00});
}

Result<void> UdsClient::clear_dtcs_uds()
{

    if (auto r = request(service::kClearDiagnosticInfo, {0xFF, 0xFF, 0xFF}); !r)
    {
        return Result<void>::error(r.error());
    }
    return {};
}

Result<Bytes> UdsClient::request_seed(uint8_t level)
{
    return request(service::kSecurityAccess, {level});
}

Result<void> UdsClient::send_key(uint8_t level, const Bytes& key)
{
    Bytes params;
    params.push_back(level);
    params.insert(params.end(), key.begin(), key.end());
    if (auto r = request(service::kSecurityAccess, params); !r)
    {
        return Result<void>::error(r.error());
    }
    return {};
}

Result<void> UdsClient::unlock(uint8_t request_level, const SeedKeyProvider& provider)
{
    auto seed_resp = request_seed(request_level);
    if (!seed_resp)
    {
        return Result<void>::error("requestSeed: " + seed_resp.error());
    }
    const Bytes& sr = *seed_resp;
    if (sr.empty())
    {
        return Result<void>::error("requestSeed: empty seed response");
    }

    Bytes seed(sr.begin() + 1, sr.end());
    if (seed.empty())
    {
        return Result<void>::error("requestSeed: no seed bytes");
    }

    auto key = provider.compute(seed);
    if (!key)
    {
        return Result<void>::error("seed→key: " + key.error());
    }

    return send_key(static_cast<uint8_t>(request_level + 1), *key);
}

Result<Bytes> UdsClient::start_routine_kwp(uint8_t lid, const Bytes& params)
{
    Bytes p{lid};
    p.insert(p.end(), params.begin(), params.end());
    return request(service::kStartRoutineByLocalId, p);
}

Result<Bytes> UdsClient::routine_results_kwp(uint8_t lid)
{
    return request(service::kRequestRoutineResults, {lid});
}

Result<Bytes> UdsClient::stop_routine_kwp(uint8_t lid, const Bytes& params)
{
    Bytes p{lid};
    p.insert(p.end(), params.begin(), params.end());
    return request(service::kStopRoutineByLocalId, p);
}

Result<Bytes> UdsClient::io_control_kwp(uint8_t lid, const Bytes& control)
{
    Bytes p{lid};
    p.insert(p.end(), control.begin(), control.end());
    return request(service::kIoControlByLocalId, p);
}

namespace
{

void push_be(Bytes& out, uint64_t value, uint8_t n)
{
    for (int i = n - 1; i >= 0; --i)
    {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

} // namespace

Result<Bytes> UdsClient::read_memory_by_address(uint64_t address, uint32_t size, uint8_t addr_bytes, uint8_t len_bytes)
{

    Bytes p;
    p.push_back(static_cast<uint8_t>(((len_bytes & 0x0F) << 4) | (addr_bytes & 0x0F)));
    push_be(p, address, addr_bytes);
    push_be(p, size, len_bytes);
    return request(service::kReadMemoryByAddress, p);
}

Result<Bytes> UdsClient::request_upload(uint64_t address, uint32_t size, uint8_t format, uint8_t addr_bytes,
                                        uint8_t len_bytes)
{
    Bytes p;
    p.push_back(format);
    p.push_back(static_cast<uint8_t>(((len_bytes & 0x0F) << 4) | (addr_bytes & 0x0F)));
    push_be(p, address, addr_bytes);
    push_be(p, size, len_bytes);
    return request(service::kRequestUpload, p);
}

Result<Bytes> UdsClient::request_download(uint64_t address, uint32_t size, uint8_t format, uint8_t addr_bytes,
                                          uint8_t len_bytes)
{
    Bytes p;
    p.push_back(format);
    p.push_back(static_cast<uint8_t>(((len_bytes & 0x0F) << 4) | (addr_bytes & 0x0F)));
    push_be(p, address, addr_bytes);
    push_be(p, size, len_bytes);
    return request(service::kRequestDownload, p);
}

Result<Bytes> UdsClient::transfer_data(uint8_t block_index, const Bytes& data)
{
    Bytes p{block_index};
    p.insert(p.end(), data.begin(), data.end());
    return request(service::kTransferData, p);
}

Result<Bytes> UdsClient::request_transfer_exit(const Bytes& params)
{
    return request(service::kRequestTransferExit, params);
}

Result<Bytes> UdsClient::read_memory_region(uint64_t address, uint32_t total, uint32_t chunk, uint8_t addr_bytes,
                                            uint8_t len_bytes)
{
    if (chunk == 0)
    {
        return Result<Bytes>::error("read_memory_region: chunk size must be non-zero");
    }
    Bytes out;
    out.reserve(total);
    uint32_t done = 0;
    while (done < total)
    {
        const uint32_t want = (total - done) < chunk ? (total - done) : chunk;
        auto r = read_memory_by_address(address + done, want, addr_bytes, len_bytes);
        if (!r)
        {
            return Result<Bytes>::error("read at 0x" +
                                        [](uint64_t v)
                                        {
                                            char b[20];
                                            std::snprintf(b, sizeof b, "%llX", static_cast<unsigned long long>(v));
                                            return std::string(b);
                                        }(address + done) +
                                        ": " + r.error());
        }
        out.insert(out.end(), r->begin(), r->end());
        done += want;
    }
    return out;
}

} // namespace cansdk
