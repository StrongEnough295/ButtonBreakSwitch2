# Guest input reader checks

Run from the repository root:

```sh
python3 libraries/gameshare/tests/guest_input/run.py
```

The tests compile the actual `libraries/gameshare/src/guest_input.cpp` as C++20
with ASan/UBSan. A synthetic shared-memory fixture and minimal public API mocks
replace the console. The production source is included in the test translation
unit so intercepted atomic loads can deterministically change a sequence or style
between its real reader's loads. No reader implementation is copied into the test.

Checks preserve the original probe suite's READ-only borrowed mapping ownership,
extreme offsets and sizes, all 33 entries for FullKey/JoyDual/Handheld, style
priority and fallback, empty and malformed ring headers, odd and changing
seqlocks, the three-attempt limit, and unmap failure/retry. They also feed 2,000
malformed headers through the actual parser. No case closes the supplied handle.

The fixtures, mocks, and tests contain synthetic data only and use no console,
network, keys, or proprietary binary. They qualify memory and lifetime behavior;
physical input reception remains a separate hardware observation. Test code and
mocks are MIT, adapted from `research/gameshare-probe/tests/native/guest_input`.
