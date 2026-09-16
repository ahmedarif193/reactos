# GIP host protocol library

This library and its public header (`sdk/include/reactos/libs/gip/gip.h`) are
MIT-licensed; see `LICENSE`. The implementation is independent of USB, HID,
Windows kernel types, and controller-driver lifetime management.

The CMake target is `gip`. Consumers link it statically and include `<gip.h>`.
`xboxhid.sys` is the first consumer and is independently MIT-licensed; see
`drivers/hid/xboxhid/LICENSE`.

## Implemented

- Checked variable-length headers, even-sized downstream headers, per-class
  USB MTUs, and coalesced upstream packets.
- Independent sequence pools per expansion device and message category;
  sequence zero is never emitted by the message sender.
- Hello decoding and per-device readiness. No command is sent before that
  device's Hello. Repeated Hellos are delivered so clients can retry startup.
- Requested acknowledgements, bounded receive reassembly, duplicate detection,
  contiguous-prefix ACKs after gaps, completion markers, and receive timeouts.
- Routing for all eight expansion indices through the message callback.
- State, metadata request, LED, four-motor and audio-format commands, plus a
  generic single-message sender and packet encoder for other consumers.
- Gamepad and Guide-button decoding. Motor levels map the full 16-bit input
  range to the protocol's 0 through 100 percent range.

## Consumer contract

The owner serializes operations on each `GIP_CONTEXT`. No library operation
waits, allocates memory, acquires an OS lock, or schedules a thread. Callbacks run
at the caller's execution level. A kernel consumer must therefore keep context,
buffers and callbacks nonpageable when receiving at DISPATCH_LEVEL.

The send callback must copy or consume the bytes before returning, and must not
reenter the context. A success result means accepted for transport, not confirmed
delivery. The transport owner handles USB failures, cancellation, and draining
completions before destroying the context.

The message callback can send commands on that context. It must not recursively
receive, reset, or replace receive buffers. Message data is borrowed for the
duration of the callback. Copy anything that must outlive it. The callback
chooses policy: a gamepad owner sends START after Hello; an audio owner performs
its audio configuration before starting. Unknown device functions remain routed
to the owner and are not implicitly treated as gamepads.

For fragmented reception, provide a buffer with `GipSetReceiveBuffer` for each
device and call `GipPoll` about every 8 ms using a monotonic millisecond clock.
One fragmented receive transaction per expansion device is supported, up to
65535 bytes, bounded by that device's supplied buffer. Other nonfragmented
messages can be delivered while a transaction is active. Reset preserves the
supplied buffers but discards readiness, sequences and partial messages.

The current Xbox HID consumer needs only single-packet input and commands. It
does not request metadata or configure fragment buffers. It ignores secondary
devices rather than interpreting their input as the primary gamepad.
The driver's existing One S / Elite initialization exception remains local to
the driver; the generic library never emits its nonstandard length field.

This is a shared protocol core, not an audio driver, GIP bus enumerator, metadata
type-handler registry, or console authentication implementation. Reliable
fragmented transmission/retransmission policy remains with the sender; the
packet encoder supports its headers, and incoming protocol-control messages are
delivered to the owner. Audio messages can be framed and routed, but audio stream
scheduling, flow control and operating-system audio integration belong to an
audio consumer.

## References

Wire formats follow Microsoft MS-GIPUSB revision 1.0 (2024-09-16), especially
sections 2.2.1, 2.2.10, 3.1.5.1–3.1.5.3, 3.1.5.5 and 3.1.5.6:
https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/e7c90904-5e21-426e-b9ad-d82adeee0dbc

The nine-byte protocol-control ACK payload is also cross-checked against the
existing host wire format (the public specification describes its behavior but
does not provide its complete field table). No third-party implementation code
is incorporated into this library.

## Native regression tests

```sh
cmake -S sdk/lib/gip/tests -B /tmp/gip-tests -DGIP_SANITIZE=ON
cmake --build /tmp/gip-tests
ctest --test-dir /tmp/gip-tests --output-on-failure
```

Tests use wire vectors for Hello/START, ACKs, gamepad input and fragmented
metadata. They cover retries, independent device and message sequence pools,
wraparound, every 16-bit motor intensity, coalesced messages, malformed/truncated
packets, extended audio headers, fragment gaps, duplicates and timeout cleanup.
