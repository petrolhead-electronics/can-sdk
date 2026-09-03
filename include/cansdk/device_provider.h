/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file device_provider.h
/// IDeviceProvider interface — abstract provider for device enumeration and connection.

#include <vector>
#include <memory>

#include "cansdk/device_info.h"

namespace cansdk
{

class IChannel; // forward declaration

/// Abstract provider — implemented by J2534Provider (Windows) or MockProvider.
class IDeviceProvider
{
public:
    virtual ~IDeviceProvider() = default;

    /// Enumerate available devices/adapters.
    [[nodiscard]] virtual std::vector<DeviceInfo> enumerate() = 0;

    /// Connect to a specific device and return a channel handle.
    [[nodiscard]] virtual std::unique_ptr<IChannel> connect(const DeviceInfo& dev) = 0;
};

} // namespace cansdk
