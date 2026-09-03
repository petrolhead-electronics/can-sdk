/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file byte_stream.h
/// IByteStream — the platform seam for raw byte transports.
///
/// An ELM327 adapter is reached over a byte pipe whose nature differs per
/// platform: a Win32 serial COM port on PC, Bluetooth LE or USB-serial on
/// Android, CoreBluetooth on iOS. Everything above this interface — the AT
/// command dialogue, response parsing, ISO-TP framing, UDS — is portable C++
/// and lives in `cansdk`. Only the implementation of this interface is
/// platform-specific.
///
/// Implementations own their connection: they are constructed already targeting
/// a port/device (a COM name, a BLE characteristic) because that addressing is
/// itself platform-specific and has no portable representation.

#include <cstddef>
#include <cstdint>

#include "cansdk/result.h"

namespace cansdk
{

/// A bidirectional stream of bytes to a device.
///
/// Implementations must be usable from a single thread; the SDK never calls
/// them concurrently.
class IByteStream
{
public:
    virtual ~IByteStream() = default;

    /// Write raw bytes. Returns the number written, or an error.
    /// Implementations should write all of `len` or report an error; a short
    /// write is reported as the count so callers can retry the remainder.
    [[nodiscard]] virtual Result<std::size_t> write(const uint8_t* data, std::size_t len) = 0;

    /// Read up to `len` bytes, waiting at most `timeout_ms`.
    /// Returning 0 is not an error — it means "nothing arrived in time".
    [[nodiscard]] virtual Result<std::size_t> read(uint8_t* buf, std::size_t len, uint32_t timeout_ms) = 0;

    /// Discard any buffered input. Called before a command exchange so a stale
    /// reply from a previous timeout cannot be mistaken for the current answer.
    virtual void flush_input()
    {
    }

    /// Close the underlying connection. Safe to call more than once.
    virtual void close() = 0;

    [[nodiscard]] virtual bool is_open() const = 0;
};

} // namespace cansdk
