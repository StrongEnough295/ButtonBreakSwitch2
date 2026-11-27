# Native display tests

Run `python3 libraries/gameshare/tests/guest_display/run.py` on the host.
The actual `guest_display.cpp` is compiled with ASan/UBSan against synthetic
VI2 calls. Tests cover renderer retirement before layer teardown, partial open,
idempotence, failed-close quarantine, callback exceptions, strong renderer
ownership, and drawing serialized with close. Deliberately quarantined private
allocations remain reachable for process-exit cleanup. No console is contacted.
