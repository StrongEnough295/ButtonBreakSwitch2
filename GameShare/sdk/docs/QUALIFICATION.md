# Device qualification

Bench date:2026-11-17. Public homebrew SDK:ounce-devkit20261115b,
libounce d1718bb7076f. The host uses the existing Kirby R-launch context.
No HEN/RO changes, saved network changes or console reboot were performed.

| Test | Measured result |
| --- | --- |
| Native Probe mode0 | Actual Default screen mirrored to the official guest for60s; native start/finalize/owned closes returned0. |
| SDK separate-image example | User reported the animated image, guest A/B response, stop and clean exit working. This tests our compiled SDK rather than a JIT-only sequence. |
| SDK example audible audio | Not tested: the example emits no audio fixture. |
| Moonlight PC stream + GameShare, Wi-Fi | Fails before matchmaking: all PC media/control channels receive ENETDOWN115. |
| Same Moonlight test, Ethernet | User reported the same disconnect before matchmaking. Ethernet alone is not a qualified workaround. |

The installed separate-image NRO was454,712 bytes,
SHA256`afd96af7362117c51ca753e95d2b01c6761f29562e4501ba40a6fa432d168e7d`.
The user answered “All is working as far as I see” after being asked to check
separate image, A/B borders and normal stop/exit. No per-command native trace
was collected from this standalone example.

The tested Moonlight NRO was18,018,771 bytes,
SHA256`dabf835751aea2716d093a8afe19d012fa6bf11eaaefc88a4a83b9bcc58e9ffb`.
Its startup log records simultaneous audio/video/control failure115, then an
unreachable PC route. Its disconnect handler cancels native startup, explaining
the absent matchmaking screen. This is not a completed PC-to-guest session.

## Network investigation

A fresh Probe under the actual Kirby caller independently confirms this on
Ethernet: before native Initialize2, NIFM reports type2/status4; immediately
after Initialize2 returns0, that same client's status query returns0xe26e.
Finalize3 returns0 and the Ethernet state begins recovering.

An owned ordinary LAN request, with connection confirmation prohibited, is
Accepted3/GetResult0 before initialization. Native Initialize2 resets it to
state1 with GetResult0x1f426e. Holding an ordinary request alone does not retain
connectivity. The exact symbolic name of that result is not yet established.
Per-client NIFM status does not by itself prove the physical interface is down.

Further finite tests find no recovery with an owned persistent request for10s
or one post-initialization re-submit. General41's raw mask stays0xf. Ordinary
System/Applet BSD/NIFM service access returns0x1015; User access succeeds.

A complete native mirror session, with one physically confirmed guest and30s
of streaming, also loses Ethernet reachability through the native host lifetime
and recovers after Finalize. Continuing past the initial PC disconnect alone
is not a qualified workaround. All native finalization and owned closes succeed.

The existing service broker also permits a System NIFM comparison with the
actual caller PID. That owned request is reset in the same way: console
ping38/80, gateway control80/80, native Finalize0 and owned kernel closes0.
This service-context comparison does not establish a networking workaround.
The SDK should not silently change system network settings to preserve a
network-backed application's input.

A separate NRO now verifies actual traffic with primary System BSD. On its
selected Wi-Fi interface, normal and Exempt UDP both pass the baseline, then
neither delivers packets during10s initialized. The same Exempt descriptor
recovers after Finalize; the normal descriptor stays shut down. Descriptor
preservation and active-route availability are separate requirements. The
System/Exempt Ethernet comparison remains untested; see [traffic evidence](../../../research/gameshare-probe/records/system-bsd-traffic-validation-20261117.json).
See the [finite experiment record](../../../research/gameshare-probe/records/native-network-validation-20261117.json)
and [continuation notes](../../../research/gameshare-probe/notes/SESSION_2026-11-17_NATIVE_NETWORK.md).

## Host validation

The native worker, read-only HID reader, display lifetime and CPU uploader have
actual-source ASan/UBSan tests. Lifecycle tests cover both video modes, startup
and cancellation, input expiry, single-host ownership, retirement before
Finalize, layer closure afterwards, and retained-resource quarantine.

The Moonlight and example NROs build for AArch64 with no unresolved symbols.
Moonlight's native Vulkan/CPU ABI, shader and packaging checks pass. These host
checks do not replace audible audio, PC-controller and network-concurrency tests.
