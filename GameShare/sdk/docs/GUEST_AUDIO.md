# Direct GameShare guest audio

The shipped SDK has a per-guest PCM path separate from applet audio mirroring.
The owning game's SDK exports `WriteGuestAudioStream` and the queue capacity,
free and filled frame queries. Its main executable calls the free-space
query and submits audio independently for guest IDs 0, 1 and 2. This
homebrew SDK continues to host one guest and submits to guest ID 0.

Static evidence from the extracted Kirby SDK, verified NSO SHA-256
`7558f288e89f81954a4d233def5f55768d67d31c00b4cc383ce9189e1f4a0dc3`:

| Scoped StreamPlay command | Verified interface |
| --- | --- |
| 40 | Guest byte at offset 0, three zero bytes, 32-bit requested frames at offset 4, one input auto-select buffer with four bytes per frame. Returns 32-bit accepted frames. |
| 41 | No inline input. Returns 32-bit queue capacity in frames. |
| 42 | Guest byte input. Returns 32-bit free frames. |
| 43 | Guest byte input. Returns 32-bit filled frames. |

The wrapper at SDK RVA `0x6aefb0` multiplies frames by four. The client
vtable at `0xdd8d10` maps its audio calls to commands 40–43. The command
40 marshaller at `0x6b7ca8` confirms the inline layout and the descriptor
attribute `0x21` (input auto-select). The retail caller at main
`0x4580f28` polls free space and advances through 16-bit storage. REA
symbol evidence is
`ev_ec1c81a0c7ff4a8602674d37e84aea10d849e7b14289819ce61aabce8a55cbca`;
wrapper and marshaller decompilation evidence is
`ev_7a53b042a93b8ad046b820556f97a4bbfdd1b459821cc0e169b67503c854d715`.
The existing receiver research identifies a 48 kHz stereo Opus path.
The app-facing input in this experiment is interleaved PCM16 stereo at
48 kHz. The native service handles delivery to the receiver.

The earlier PPSSPP console tests found silence on the client with both
AudOut and Audren when relying on application-mix mirroring. The direct
mode leaves that flag disabled. The native worker queries free space after
the guest connects, limits queued audio, and retains unwritten PCM after a
partial write. If direct audio fails, the worker logs the Result and leaves
the separate video and controller working. A successful write confirms
queue acceptance. The physical audio check determines whether the guest
actually heard the samples and whether the host retained only its own mix.
