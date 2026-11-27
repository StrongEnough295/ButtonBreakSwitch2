# Recovered native host contract

These independently written interoperability notes accompany our implementation;
they contain no Nintendo SDK code. Query native sizes on every initialization.
The public API implements one local guest with Default mirroring or an
application-rendered separate guest display.

## Identity and account

The qualified caller uses the installed GameShare-capable Kirby title's existing
homebrew launch path. An arbitrary title is not established as sufficient.
There is no control-property rewrite, HEN/RO update or account edit.

On an owned acc:u0 session,160 takes eight zero bytes plus SendPid to bind the
application.3 lists open users;150 reads the account switch lock;2 lists local
users. An unambiguous open user takes priority. If user switching is locked
and the open-user list is ambiguous or empty, use the profile Nintendo supplied
for this application through the preselected-user launch parameter. Validate
and cache it in memory for retries in the same process. If the loader
provides its in-process user-ID cache, preserve the launch selection across
NRO loads. A locked title without a valid launch profile fails before StreamPlay. Titles that allow switching can still
use normal player selection.

Switch2 player selection uses applet0x01000010/common version0x20000. Retain
the owned application manager returned by101 with the real16-byte UID until
the entire GameShare session ends. The SDK offers no new-user or online flow.

For parental communication permission, open the available pctl variant, convert
the root to domain and call0 with eight zero bytes/SendPid. On its child call
1 Initialize,1001 permission check,1021 begin, and1022 end. Denial is terminal.

## StreamPlay commands

Owned stpl:u root command0 takes u32 factory parameter3 and returns the scoped
interface. **Factory3 does not select wire-protocol3.**

| Scoped ID | Input | Output / purpose |
| --- | --- | --- |
|1|MemoryConfig8|u32 required work bytes|
|2|InitializeConfig16 + SendPid|Initialize|
|3|none|Finalize before freeing buffers|
|4|u32 guest index|Enable guest streaming|
|5|none|Close session|
|7|none|Copy connection-event handle|
|8|none|u32 guest mask + u32 native error|
|9|MemoryConfig8 with guests0|Four u64 region sizes|
|25 /26|u32 style mask /none|Set /get supported controller styles|
|27|u32 value1|Controller orientation|
|300|none|Two u64 values(size,offset) + copy HID handle|
|500|none|StartStreaming|
|501|none|u32 maximum guests|
|502 /503|none|u8 initialized type /mode|
|504|MatchConfig32|128 genuine matchmaking argument bytes|
|505|u64 size + one copy transfer handle|Register global buffer|
|506|GuestBuffers32 + three copy transfer handles|Register guest buffers|
|510|AudioConfig16|Current output configuration|
|40–43|Guest PCM and queue queries|[Direct guest audio contract](GUEST_AUDIO.md)|
|511|Recovered64-byte extended request|Alternate matching; unused here|

MemoryConfig8 is `{u8 type,u8 mode,u16 zero,u32 guests}`. InitializeConfig16
appends a zero u64 PID placeholder. Local type0/mode0 mirrors the Default
display. Mode1 is the separately rendered guest display path qualified by the
earlier Probe color-bar/input tests. The SDK selects it through the separate
video mode and retains the configured video target through shutdown/retirement.
The adapter/renderer must own and retire its submitted images; this is not an
encoded-media injection interface.

Command1 uses the actual guest count;9 uses guests0. Validate page alignment,
overflow, reasonable allocation limits and equality of the sum with command1.
Allocate and zero aligned memory. The region/handle order is **global, first,
third, second**. Create Perm_None transfer memory for nonzero regions. Preserve
all three guest handle positions, including nulls for zero-size regions.
GuestBuffers32 is `{u32 guest,u32 zero,u64 sizes[3]}` in that same reordered
sequence. Never free these allocations before successful Finalize.

MatchConfig32 contains four u8 minima/maxima(all1), u32 zero, u32 flags0,
u16 features3, u16 other0, then the actual16-byte account UID. The returned
128 bytes are valid only for this live native session. Never replay them after
Finalize or in another process.

## Applet, media and input

Create GameShare applet0x01000024 in AllForeground mode with common version
0x10000, then push the genuine128 bytes. Wait for normal completion. Cancellation
uses RequestExit and a bounded wait, never forced termination. Retain resources
if the applet does not exit. Read accessor GetResult30; an unpopulated cached
exit reason is insufficient. Pop/validate output storage. Its first eight bytes
are u32 session information and signed guest count; require one guest for these
allocations. Query502/503, enable guest0 with4, then call500.

Poll8 for actual mask/errors. Successful500 alone does not prove media or input.
Allow initial settling; neutralize on disconnect/error. Our consumer also checks
snapshot age so a blocked native IPC cannot leave a held button indefinitely.

Borrow appletGetServiceSession_SelfController() without closing it.2401 takes
exactly one inline u8 boolean, no PID/buffers/output, to enable this application's
mirrored audio. Disable it during cleanup. AudioConfig16 is
`{u8 autoTune,u8 zero[3],float speaker,float tv,float external}`. Read existing
values and forward510; do not change system settings. The qualified fallback
is `aud:u` command2 to obtain the renderer manager, manager command2 with the
application's resource-user ID to obtain a device, device command8 to read the
named output volume and command20 to read auto-tune. Names are AudioBuiltInSpeakerOutput, AudioTvOutput and
AudioExternalOutput. All created audio sessions are owned and closed.

Map300's HID memory read-only. Guest0 FullKey/Handheld/JoyDual use bounded
seqlock reads. Buttons/axes retain native semantics. Applications decide how to
map them; this SDK assigns no PC controller IDs. Rumble/motion/mouse/touch are
not implemented. There is no recovered compressed video/audio injection API.

## Stop and compatibility

CloseSession5, disable own mirrored audio, unmap HID, then retire the separate
target's submitted images when configured, keeping its VI2 layer/display live.
End communication1022, Finalize3, then close the target's layer/display, close
handles and free allocations. The public VideoTarget phases are open(),
retire() before Finalize, and close() after Finalize; all run on the worker and
cleanup must handle partial open. Retirement failure stops before Finalize;
closure failure stops after Finalize. Both retain remaining resources. Close
children before parent sessions. Failed finalization retains referenced
allocations and quarantines all
Host instances until the hosting process exits. The implementation remains
pinned and canUnload() stays false. Do not return to hbmenu, unload the NRO,
destroy renderer/backing state or attempt a fresh Host as a workaround. Close
the entire hosting title/process through HOME. Only normal successful cleanup
followed by canUnload()==true permits ordinary teardown.

The native service selects its wire protocol. Older guests can request a system
update despite having GameShare. Neither factory3 nor a cosmetic firmware label
is a qualified protocol downgrade. See [research findings](RESEARCH.md).
