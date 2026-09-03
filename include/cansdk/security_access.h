/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file security_access.h
/// UDS/KWP service 0x27 (SecurityAccess) seed→key providers.
///
/// A SeedKeyProvider maps an ECU-issued seed to the key the tester must return.
/// Two implementations are provided:
///   - Rc4TableSeedKey: reproduces the MelcoDiag scheme (a 64K-entry table of
///     RC4-encrypted ASCII-hex keys; see docs/ducati-diag-protocol.md §6).
///   - FixedSeedKey: returns a constant key (test/bench use).
///
/// Providers are pure computation — no bus access — so they are trivially
/// testable and safe to run offline.

#include <cstdint>
#include <string>
#include <vector>

#include "cansdk/result.h"

namespace cansdk
{

/// Abstract seed→key transform for SecurityAccess (service 0x27).
class SeedKeyProvider
{
public:
    virtual ~SeedKeyProvider() = default;

    /// Compute the key bytes for a given seed.
    /// @param seed Raw seed bytes as received in the 0x67 response (after the
    ///             sub-function byte).
    /// @return Key bytes to send in the 0x27 sendKey request, or an error.
    [[nodiscard]] virtual Result<std::vector<uint8_t>> compute(const std::vector<uint8_t>& seed) const = 0;
};

/// RC4 stream cipher (symmetric: encrypt == decrypt). Exposed for reuse/testing.
/// Reproduces the "Class1" cipher embedded in MelcoDiag.
[[nodiscard]] std::vector<uint8_t> rc4(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);

/// Table-backed provider reproducing MelcoDiag's security access.
///
/// The table is a flat blob of `65536 * entry_size` bytes (Msdll.dll is
/// 65536 * 4). A 16-bit seed indexes it: `entry = table[seed * entry_size ...]`.
/// Each entry is RC4-decrypted with `cipher_key` to yield the ASCII-hex key
/// string (e.g. "A1BA"), which is parsed back into `entry_size/2` key bytes.
class Rc4TableSeedKey : public SeedKeyProvider
{
public:
    /// @param table      Raw key table (e.g. contents of Msdll.dll).
    /// @param cipher_key RC4 key (MelcoDiag uses "sys").
    /// @param entry_size Bytes per table entry (MelcoDiag: 4 ASCII-hex chars).
    Rc4TableSeedKey(std::vector<uint8_t> table, std::string cipher_key = "sys", std::size_t entry_size = 4);

    /// Load the table from a file (e.g. the shipped Msdll.dll).
    [[nodiscard]] static Result<Rc4TableSeedKey> from_file(const std::string& path, std::string cipher_key = "sys",
                                                           std::size_t entry_size = 4);

    [[nodiscard]] Result<std::vector<uint8_t>> compute(const std::vector<uint8_t>& seed) const override;

    /// Direct 16-bit seed → key-bytes lookup (used by compute()).
    [[nodiscard]] Result<std::vector<uint8_t>> lookup(uint16_t seed) const;

private:
    std::vector<uint8_t> table_;
    std::vector<uint8_t> cipher_key_;
    std::size_t entry_size_;
};

/// Constant-key provider for testing/bench setups.
class FixedSeedKey : public SeedKeyProvider
{
public:
    explicit FixedSeedKey(std::vector<uint8_t> key) : key_(std::move(key))
    {
    }

    [[nodiscard]] Result<std::vector<uint8_t>> compute(const std::vector<uint8_t>&) const override
    {
        return key_;
    }

private:
    std::vector<uint8_t> key_;
};

} // namespace cansdk
