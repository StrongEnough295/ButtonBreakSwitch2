# CPU RGBA uploader tests

Run `python3 libraries/gameshare/tests/cpu_image_streamer/run.py` on the host.
The actual C uploader is included in a synthetic GSV/VI2 fixture and tested with
ASan/UBSan. It verifies padded source rows, invalid dimensions/stride/overflow,
independent known tiling vectors, image bounds, normal retirement, uncertain
present quarantine, owner identity, and a timed-out range retirement retried
without replaying its request. Mock process death releases deliberately retained
test backing; this does not represent permission to free native live backing.

Public API fixtures and the retained CPU implementation derive from libounce
commit `d1718bb7076fb356cb4cb81ab13d035db5367eea`; see the preserved ISC notices
and `LICENSE`. These tests neither establish a new private SDK ABI nor replace
the earlier physical guest rendering baseline. They never contact a console.
