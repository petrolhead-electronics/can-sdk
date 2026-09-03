/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file uds_client.h
/// Generic UDS (ISO 14229) / KWP2000 (ISO 14230) client over ISO-TP.
///
/// Ducati ECUs speak a mix of UDS and KWP tunneled over CAN; both share request
/// structure (service byte + parameters), so one client covers both with a small
/// set of stack-specific helpers. See docs/ducati-diag-protocol.md.
///
/// Read-only helpers (identification, DTC read) are safe. State-changing helpers
/// (session control, security access, routines, I/O control, clear DTC) transmit
/// and must be reached through the gated diag_service.h in production paths.

#include <cstdint>
#include <string>
#include <vector>

#include "cansdk/result.h"
#include "cansdk/isotp.h"
#include "cansdk/security_access.h"

namespace cansdk
{

using Bytes = std::vector<uint8_t>;

/// Common service IDs (request side).
namespace service
{
constexpr uint8_t kDiagnosticSessionControl = 0x10; // UDS
constexpr uint8_t kEcuReset = 0x11;                 // UDS
constexpr uint8_t kClearDiagnosticInfo = 0x14;      // UDS
constexpr uint8_t kReadDtcInformation = 0x19;       // UDS
constexpr uint8_t kReadEcuIdentification = 0x1A;    // KWP
constexpr uint8_t kReadDtcByStatus = 0x18;          // KWP
constexpr uint8_t kReadDataByIdentifier = 0x22;     // UDS
constexpr uint8_t kSecurityAccess = 0x27;           // UDS/KWP
constexpr uint8_t kInputOutputControl = 0x2F;       // UDS
constexpr uint8_t kIoControlByLocalId = 0x30;       // KWP
constexpr uint8_t kRoutineControl = 0x31;           // UDS
constexpr uint8_t kStartRoutineByLocalId = 0x31;    // KWP (start)
constexpr uint8_t kStopRoutineByLocalId = 0x32;     // KWP (stop)
constexpr uint8_t kRequestRoutineResults = 0x33;    // KWP
constexpr uint8_t kStopDiagnosticSession = 0x20;    // KWP
constexpr uint8_t kTesterPresent = 0x3E;            // UDS/KWP
constexpr uint8_t kReadMemoryByAddress = 0x23;      // UDS
constexpr uint8_t kRequestDownload = 0x34;          // UDS (write into ECU)
constexpr uint8_t kRequestUpload = 0x35;            // UDS (read out of ECU)
constexpr uint8_t kTransferData = 0x36;             // UDS
constexpr uint8_t kRequestTransferExit = 0x37;      // UDS
} // namespace service

constexpr uint8_t kPositiveOffset = 0x40;
constexpr uint8_t kNegativeResponse = 0x7F;
constexpr uint8_t kNrcResponsePending = 0x78;

/// Client bound to one ECU (one ISO-TP transport / address pair).
class UdsClient
{
public:
    explicit UdsClient(IsoTpTransport& transport) : tp_(transport)
    {
    }

    IsoTpTransport& transport()
    {
        return tp_;
    }

    /// Send `service` + `params`, validate the positive-response echo, and
    /// return the response payload (bytes after the echoed service id).
    /// Transparently retries while the ECU replies 0x78 (responsePending).
    [[nodiscard]] Result<Bytes> request(uint8_t service, const Bytes& params = {});

    // --- Session / keep-alive -------------------------------------------------
    [[nodiscard]] Result<Bytes> diagnostic_session(uint8_t sub); // 0x10
    [[nodiscard]] Result<void> stop_diagnostic_session();        // 0x20
    [[nodiscard]] Result<void> tester_present();                 // 0x3E 00

    // --- Identification (read-only) ------------------------------------------
    [[nodiscard]] Result<Bytes> read_data_by_id(uint16_t did);       // 0x22
    [[nodiscard]] Result<Bytes> read_ecu_identification(uint8_t id); // 0x1A
    /// Read VIN via UDS 22 F190; returns the ASCII VIN bytes (DID echo stripped).
    /// (Returned as bytes because Result<std::string> is not representable with
    /// this codebase's variant-based Result.)
    [[nodiscard]] Result<Bytes> read_vin();

    // --- DTCs -----------------------------------------------------------------
    [[nodiscard]] Result<Bytes> read_dtcs_uds(uint8_t status_mask = 0xFF); // 0x19 02
    [[nodiscard]] Result<Bytes> read_dtcs_kwp();                           // 0x18 00 FF 00
    [[nodiscard]] Result<void> clear_dtcs_uds();                           // 0x14 FF FF FF

    // --- Security access (0x27) ----------------------------------------------
    [[nodiscard]] Result<Bytes> request_seed(uint8_t level);              // 0x27 <odd>
    [[nodiscard]] Result<void> send_key(uint8_t level, const Bytes& key); // 0x27 <even>
    /// Full seed→key handshake: request seed, compute via provider, send key.
    [[nodiscard]] Result<void> unlock(uint8_t request_level, const SeedKeyProvider& provider);

    // --- Routines / actuators -------------------------------------------------
    /// KWP StartRoutineByLocalId (0x31 <lid> <params>).
    [[nodiscard]] Result<Bytes> start_routine_kwp(uint8_t lid, const Bytes& params = {});
    /// KWP RequestRoutineResults (0x33 <lid>).
    [[nodiscard]] Result<Bytes> routine_results_kwp(uint8_t lid);
    /// KWP StopRoutineByLocalId (0x32 <lid> <params>).
    [[nodiscard]] Result<Bytes> stop_routine_kwp(uint8_t lid, const Bytes& params = {});
    /// KWP InputOutputControlByLocalId (0x30 <lid> <control>...).
    [[nodiscard]] Result<Bytes> io_control_kwp(uint8_t lid, const Bytes& control);

    // --- Memory / data transfer ----------------------------------------------
    //
    // These are the services a reprogramming or memory-read session uses. They
    // are protocol primitives, exactly as any UDS library exposes them; the
    // policy question (whether to *write* firmware) lives in the workflow layer,
    // not here. See diag_session.h.
    //
    // The addressAndLengthFormatIdentifier is expressed as byte widths:
    // `addr_bytes` and `len_bytes` say how many bytes encode the address and the
    // size, which must match what the ECU expects (commonly 4 and 4, or 4 and 2).

    /// ReadMemoryByAddress (0x23). Reads `size` bytes from `address`.
    [[nodiscard]] Result<Bytes> read_memory_by_address(uint64_t address, uint32_t size, uint8_t addr_bytes = 4,
                                                       uint8_t len_bytes = 4);

    /// RequestUpload (0x35) — begin reading a block OUT of the ECU. Returns the
    /// raw positive response, whose lengthFormatIdentifier tells you the ECU's
    /// maximum TransferData block size.
    [[nodiscard]] Result<Bytes> request_upload(uint64_t address, uint32_t size, uint8_t format = 0x00,
                                               uint8_t addr_bytes = 4, uint8_t len_bytes = 4);

    /// RequestDownload (0x34) — begin writing a block INTO the ECU. Provided for
    /// completeness and for characterising what an ECU permits; the tool does not
    /// orchestrate flashing (see diag_session.h and SAFETY.md).
    [[nodiscard]] Result<Bytes> request_download(uint64_t address, uint32_t size, uint8_t format = 0x00,
                                                 uint8_t addr_bytes = 4, uint8_t len_bytes = 4);

    /// TransferData (0x36) — one block. `block_index` is the sequence counter
    /// starting at 1; `data` is empty when reading (uploading).
    [[nodiscard]] Result<Bytes> transfer_data(uint8_t block_index, const Bytes& data = {});

    /// RequestTransferExit (0x37) — end a transfer.
    [[nodiscard]] Result<Bytes> request_transfer_exit(const Bytes& params = {});

    /// Read a whole region by looping ReadMemoryByAddress in `chunk` steps.
    /// Read-only: it cannot change ECU state. Prerequisites (an unlocked
    /// programming/extended session) are the caller's responsibility.
    [[nodiscard]] Result<Bytes> read_memory_region(uint64_t address, uint32_t total, uint32_t chunk = 0x40,
                                                   uint8_t addr_bytes = 4, uint8_t len_bytes = 4);

private:
    IsoTpTransport& tp_;
};

/// Decode a UDS negative-response code to a short human string.
[[nodiscard]] const char* nrc_to_string(uint8_t nrc);

} // namespace cansdk
