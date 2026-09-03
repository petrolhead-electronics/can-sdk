/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/security_access.h"

#include <array>
#include <cctype>
#include <fstream>

namespace cansdk
{

std::vector<uint8_t> rc4(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data)
{
    std::array<int, 256> s{};
    for (int i = 0; i < 256; ++i)
    {
        s[i] = i;
    }

    if (!key.empty())
    {
        int j = 0;
        for (int i = 0; i < 256; ++i)
        {
            j = (j + s[i] + key[i % key.size()]) & 0xFF;
            std::swap(s[i], s[j]);
        }
    }

    std::vector<uint8_t> out;
    out.reserve(data.size());
    int i = 0, j = 0;
    for (uint8_t b : data)
    {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        std::swap(s[i], s[j]);
        out.push_back(static_cast<uint8_t>(b ^ s[(s[i] + s[j]) & 0xFF]));
    }
    return out;
}

namespace
{

bool parse_hex(const std::string& s, std::vector<uint8_t>& out)
{
    if (s.size() % 2 != 0)
    {
        return false;
    }
    auto nib = [](char c) -> int
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
    };
    for (std::size_t k = 0; k < s.size(); k += 2)
    {
        int hi = nib(s[k]);
        int lo = nib(s[k + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

} // namespace

Rc4TableSeedKey::Rc4TableSeedKey(std::vector<uint8_t> table, std::string cipher_key, std::size_t entry_size)
    : table_(std::move(table)), cipher_key_(cipher_key.begin(), cipher_key.end()), entry_size_(entry_size)
{
}

Result<Rc4TableSeedKey> Rc4TableSeedKey::from_file(const std::string& path, std::string cipher_key,
                                                   std::size_t entry_size)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return Result<Rc4TableSeedKey>::error("cannot open key table: " + path);
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 65536u * entry_size)
    {
        return Result<Rc4TableSeedKey>::error("key table too small: " + std::to_string(data.size()) + " bytes");
    }
    return Rc4TableSeedKey(std::move(data), std::move(cipher_key), entry_size);
}

Result<std::vector<uint8_t>> Rc4TableSeedKey::lookup(uint16_t seed) const
{
    const std::size_t off = static_cast<std::size_t>(seed) * entry_size_;
    if (off + entry_size_ > table_.size())
    {
        return Result<std::vector<uint8_t>>::error("seed out of table range");
    }

    std::vector<uint8_t> enc(table_.begin() + off, table_.begin() + off + entry_size_);
    std::vector<uint8_t> dec = rc4(cipher_key_, enc);
    std::string hex(dec.begin(), dec.end());

    std::vector<uint8_t> key;
    if (!parse_hex(hex, key))
    {
        return Result<std::vector<uint8_t>>::error("decoded key is not valid hex for seed " + std::to_string(seed));
    }
    return key;
}

Result<std::vector<uint8_t>> Rc4TableSeedKey::compute(const std::vector<uint8_t>& seed) const
{
    if (seed.empty() || seed.size() > 2)
    {
        return Result<std::vector<uint8_t>>::error("Rc4TableSeedKey expects a 1- or 2-byte seed");
    }

    uint16_t s = seed[0];
    if (seed.size() == 2)
    {
        s = static_cast<uint16_t>((seed[0] << 8) | seed[1]);
    }
    return lookup(s);
}

} // namespace cansdk
