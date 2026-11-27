# Research handoff and compatibility boundaries

The SDK packages the recovered native client sequence, bounded guest-HID reader and separate guest-display adapter. It does not package the private executable dumps used to qualify their ABI. Research notes distinguish static reconstruction, host tests, console return values and user-observed output; those evidence types are not interchangeable.

## Native route

The recovered native sequence uses an existing application launch context and a real selected user. It initializes an app-scoped account client, retains that user's manager, creates a native StreamPlay client, allocates/registers queried work memory, performs normal parental communication checks, obtains fresh native matchmaking arguments, opens the stock local applet, and starts streaming.

| Finding | Implementation consequence |
| --- | --- |
| `stpl:u` factory command 0 takes `u32=3` | Use the measured service ABI; this value is not wire protocol major 3. |
| Account command 160 binds the application, then 101 retains the chosen user | Use an existing, unambiguous profile or the normal cancellable selector. Last-opened user is not selected identity. |
| Command 300 returns shared-memory size/format offset and a copy handle | Validate bounds, map read-only, and distinguish mapping ownership from handle ownership. |
| Matchmaking command 504 takes 32 bytes and returns 128 native argument bytes | Keep these arguments with their live preparation; never cache/replay them across finalized sessions. |
| Local matchmaking applet is `0x01000024` with common version `0x10000` | Pass the full Switch 2 applet ID. Legacy IDs are not automatically translated. |
| Player selector is `0x01000010` with common version `0x20000` | Own its holder so stop and timeout can request normal exit. |
| Start is per-guest command 4, then 500; connection state is command 8 | A successful start is separate from the first connected event and first HID sample. |
| Close/finalize precedes releasing work memory | Retain allocations after uncertain or failed cleanup. |

Detailed provenance and measured corrections are in [the owned-SDK continuation](../../../research/gameshare-probe/notes/SESSION_2026-11-16_OWNED_SDK.md) and [its portable validation record](../../../research/gameshare-probe/records/owned-sdk-native-validation-20261116.json). Earlier reconstructed configuration guesses and the former `0x2fab1f` failure are historical context, not the current initialization recipe.

The Probe demonstrated real animated guest frames, controller button changes, and successful normal cleanup. In the [2026-11-17 native-mirror run](../../../research/gameshare-probe/notes/SESSION_2026-11-17_NATIVE_MIRROR.md), job 2 in session `4b048efdf87f3de7` returned 0 and observed guest mask 1/error 0 for 60 seconds. The user confirmed that the guest connected and saw the mirrored screen. CloseSession, communication-end, Finalize and owned closes succeeded. Audio-sharing enable/disable also returned 0, but no audible fixture was confirmed.

The user subsequently confirmed the compiled SDK's separate-image example,
guest A/B response and normal stop/exit. Concurrent Moonlight PC streaming fails
during native initialization on both Wi-Fi and Ethernet; mirrored PC audio and
guest-to-PC input remain unverified. The [network continuation](../../../research/gameshare-probe/notes/SESSION_2026-11-17_NATIVE_NETWORK.md)
records the accepted app-owned NIFM request being reset during Initialize2.
See [current device qualification](QUALIFICATION.md) before treating any of
these native results as a completed PC-to-guest session.

The read-only NIFM error-metadata query supplies only a generic Result viewer
argument for2110-4001, with no extra context. The [sanitized error record](../../../research/gameshare-probe/records/native-network-error-applet-20261117.json)
qualifies its32B Switch2 layout without launching the applet. The [older-firmware
server audit](../../../research/gameshare-probe/records/nifm-server-arbitration-audit-20261117.json)
explains the persistent request's OnHold behavior and bounds the Greedy/Sharable
hypothesis. A separate primary System BSD traffic comparison is prepared; no
network-concurrency workaround has been demonstrated.

The earlier mode 1 Probe path rendered a separate animated RGBA8 image on the guest's VI2 layer. Its renderer borrowed the existing application GPU/synchronization context while owning its own images/backing. Presentation completion and registration retirement preceded release; uncertain retirement retained the resources. The qualified ordering retires renderer images before StreamPlay Finalize, but keeps the VI2 layer/display alive until after Finalize. The reusable target preserves this distinction through separate `retire()` and `close()` phases. It exposes an application-rendered image route; it does not establish a compressed H.264/AV1 injection API or qualify an arbitrary renderer's synchronization.

## Firmware and protocol versions

Selected signed Switch 1 Splay artifacts establish these sample points:

| Guest firmware sample | Default LRP major |
| --- | ---: |
| 20.0.0 | 2 |
| 20.1.1 / 20.2.0 | 3 |
| 21.0.0 / 22.0.0 / 22.5.0 | 4 |
| 23.0.1 | 7 |

These are artifact-backed samples, not a supported minimum-firmware range or an exhaustive release history. The inspected discovery/connection paths compare host and receiver majors before accepting the session. The native host's actual on-air advertisement was not captured by the earlier monitor, so the older guest's update dialog is not itself a packet-level measurement of that host's major.

The SDK uses the installed native service's compatibility policy. It exposes no protocol-version override. The factory's input 3 is not a request to advertise `0x30000`. System-wide LRP override settings were observed disabled and are not changed by this SDK.

Protocol 2 and 3 already contain matching video, audio, layout/crop and video-metadata parser paths. This research does not establish a mirror-only protocol 1 tier or assign features by increasing major number. See [protocol versions and media comparison](../../../research/gameshare-probe/notes/SESSION_2026-11-17_PROTOCOL_VERSIONS.md), [verified artifact identities](../../../research/gameshare-probe/records/protocol-version-matrix-20261117.json), and [gate records](../../../research/gameshare-probe/records/protocol-gates-20261117.json).

## Independent custom-host work

The application-owned protocol prototype is separate from this native wrapper. Its offline version-aware encoders and bounded on-console TCP error replies do not establish authenticated guest admission or media delivery. A reserved GameShare local-communication ID `0x4d` startup attempt returned `0xc2cb`, while a caller-owned-ID control succeeded. The observed authorization boundary was not an occupied native network and was not removed by closing another host.

No qualified app-scoped native version selector, custom-advertisement callback or arbitrary encoded-media injection path was recovered. The public LRP creator fields and host commands remain only partly decoded. The SDK therefore does not expose opaque setters, rewrite NACP/ARP identity or replace the native transport.

See [the custom version 3 attempt](../../../research/gameshare-probe/notes/SESSION_2026-11-17_CUSTOM_V3_ATTEMPT.md), [its portable record](../../../research/gameshare-probe/records/custom-v3-host-attempt-20261117.json), [the native-route audit](../../../research/gameshare-probe/records/native-v3-route-audit-20261117.json), and [the independent protocol research](../../../research/gameshare-host/RESEARCH.md).

## Portable evidence and continuation

- [Probe continuation index](../../../research/gameshare-probe/START_HERE.md): chronological outcomes and reproduction boundaries. Read newer native notes before historical failed guesses.
- [Probe portable records](../../../research/gameshare-probe/records/README.md): sanitized source identities, validation and console-result summaries.
- [Public native-service fidelity sources](../../../research/gameshare-host/records/native-sdk-fidelity-sources-20261116.json): pinned public corroboration.
- [Switch 2 application/toolchain guide](../../../docs/SWITCH2.md): the public homebrew SDK, native rendering and build setup.

Keep private user IDs, pairing keys, raw applet arguments, executable/firmware dumps and console secrets out of the SDK and portable records. SDK use requires no HEN/RO replacement, firmware update, reboot, save edit, downloaded Nintendo code or new acquisition pipeline. The library operates through the application's existing authorized native interfaces.
