# cansdk

A small, portable **CAN diagnostics SDK** in C++20. No platform code, no
dependencies — you supply the transport, it does the protocol work.

## What's in it

| Area | Header | What it gives you |
|---|---|---|
| Frames | `cansdk/can_frame.h` | `CanFrame`, `BusProtocol`, frame types |
| Channel | `cansdk/channel.h` | `IChannel` — the transport interface (implement it over J2534, SocketCAN, …) |
| Byte stream | `cansdk/byte_stream.h` | `IByteStream` — a serial/BLE/USB pipe for ELM327 |
| ISO-TP | `cansdk/isotp.h` | ISO 15765-2 segmentation + flow control, with native-adapter offload |
| UDS/KWP | `cansdk/uds_client.h` | a request/response client for UDS (ISO 14229) and KWP2000 |
| Security | `cansdk/security_access.h` | SecurityAccess seed→key scaffolding |
| ELM327 | `cansdk/elm327_channel.h` | an `IChannel` driving an ELM327 over an `IByteStream` |
| Tracing | `cansdk/frame_trace.h` | directioned (Tx/Rx) frame logging |
| Result | `cansdk/result.h` | a lightweight `Result<T>` error type |
| Units | `cansdk/units.h` | automotive unit conversions |

Everything lives in namespace `cansdk`.

## Using it

```cmake
add_subdirectory(cansdk)        # or FetchContent / a git submodule
target_link_libraries(your_app PRIVATE cansdk::cansdk)
```

```cpp
#include <cansdk/isotp.h>
#include <cansdk/uds_client.h>

cansdk::IsoTpTransport tp(channel, {.tx_id = 0x7E0, .rx_id = 0x7E8});
cansdk::UdsClient uds(tp);
auto vin = uds.request(0x22, {0xF1, 0x90});
```

## Design

- **You own the transport.** `IChannel` and `IByteStream` are pure interfaces;
  the SDK never opens a port. That keeps it platform-free and testable with an
  in-memory scripted channel.
- **Native ISO-TP offload.** When a channel reports `supportsHwIsoTp()`, whole
  PDUs are handed to the adapter so it segments at line rate — which is what
  makes a flash write take on a slow bridge.

## License

MIT. See [LICENSE](LICENSE).
