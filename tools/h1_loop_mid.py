#!/usr/bin/env python3
"""h1's loop, as the FM-1 sends it over USB MIDI (app.c, "the loop out to a computer"), made into a .mid.

    tools/h1_loop_mid.py FRAMES.syx [OUT.mid]   the SysEx frames (as a MIDI monitor saves them, or the host
                                                simulator's sysex_log), checked and turned into a file

For the tests (tests/run_tests.sh) and for anyone with the frames saved from a MIDI monitor. A format 1 file at 96 ticks to the
quarter note (h1's own clock), the tempo and meter of the loop, one track per layer (and one for notes
played without LAYERS), named after its instrument and algorithm, on its own channel."""
import struct
import sys
import zlib

INSTRUMENTS = ["COZY", "ORBIT", "KOI", "HAUNT", "TIDE", "PIP", "BONK", "URCHIN", "TWANG", "LAVA", "KNOT", "INK",
               "BARD", "TURBO", "TILT", "PLUNK"]
METERS = [(4, 4), (3, 4), (5, 4), (6, 8), (7, 8)]
LOOP_MAGIC = 0x504C3148                         # "H1LP", synth.c


def unpack7(b):
    out = bytearray()
    for i in range(0, len(b), 8):
        hi, low = b[i], b[i + 1:i + 8]
        for k, v in enumerate(low):
            out.append(v | ((hi >> k) & 1) << 7)
    return bytes(out)


def frames(raw):
    i = 0
    while True:
        a = raw.find(b"\xF0\x7D\x68\x31", i)
        if a < 0:
            return
        e = raw.find(b"\xF7", a)
        if e < 0:
            return
        yield raw[a + 4], raw[a + 5:e]
        i = e + 1


def read(raw):
    meta = None
    size = 0
    chunks = {}
    crc = None
    for kind, body in frames(raw):
        kind = chr(kind)
        if kind == "X":
            raise ValueError("the FM-1 gave up: the loop changed while it went, or nobody read")
        if kind == "H":
            d = unpack7(body)
            meta, size = d[:54], struct.unpack("<I", d[54:58])[0]
            chunks, crc = {}, None
        elif kind == "D":
            chunks[body[0] | body[1] << 7] = unpack7(body[2:])
        elif kind == "E":
            crc = struct.unpack("<I", unpack7(body)[:4])[0]
    if meta is None or crc is None:
        raise ValueError("not a whole loop: no header or no end")
    blob = b"".join(chunks[i] for i in sorted(chunks))
    if len(blob) != size or zlib.crc32(blob) != crc:
        raise ValueError(f"damaged: {len(blob)} of {size} bytes, CRC {zlib.crc32(blob):08X} against {crc:08X}")
    if struct.unpack("<HH", blob[:4]) != (LOOP_MAGIC & 0xFFFF, LOOP_MAGIC >> 16) or blob[4] not in (1, 2):
        raise ValueError("not an h1 loop")
    nev, loop_len = struct.unpack("<HH", blob[6:10])
    events = [struct.unpack("<HBBBBBB", blob[12 + 8 * i:20 + 8 * i]) for i in range(nev)]
    return meta, loop_len, events


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


def track(evs, name=None):
    data = bytearray()
    if name:
        nm = name.encode()
        data += b"\x00\xFF\x03" + vlq(len(nm)) + nm
    t = 0
    for at, msg in sorted(evs, key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90)):
        data += vlq(at - t) + bytes(msg)
        t = at
    data += b"\x00\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + data


def part_name(part):
    if not part:
        return "h1"
    inst, algo = (part - 1) // 8, (part - 1) % 8
    return (INSTRUMENTS[inst] if inst < len(INSTRUMENTS) else f"INSTRUMENT {inst + 1}") + f" {algo + 1}"


def to_mid(meta, loop_len, events):
    tempo, meter = meta[1], meta[2] if meta[2] < 5 else 0
    num, den = METERS[meter]
    us = 60000000 // max(40, tempo)
    head = [(0, [0xFF, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255]),
            (0, [0xFF, 0x58, 4, num, {4: 2, 8: 3}[den], 24, 8])]
    groups = {}
    for tick, note, on, part, lay, vel, _ in events:
        groups.setdefault(lay if lay else -part, []).append((tick, note, on & 1, vel))
    tracks = [track(head + [(loop_len, [0xFF, 0x06, 3] + list(b"END"))], "h1 loop")]   # (a marker where it goes round)
    for ch, (key, evs) in enumerate(sorted(groups.items())):
        part = meta[6 + key - 1] if key > 0 else -key
        name = (f"LAYER {key} " if key > 0 else "") + part_name(part)
        c = ch % 15 + (ch % 15 >= 9)              # (channel 10 kept for drums)
        out, held = [], {}
        for tick, note, on, vel in sorted(evs, key=lambda e: e[0]):
            if on:
                if note in held:
                    out.append((tick, [0x80 | c, note, 0]))
                held[note] = tick
                out.append((tick, [0x90 | c, note, vel or 127]))
            elif note in held:
                del held[note]
                out.append((tick, [0x80 | c, note, 0]))
        for note in held:                         # (a note-off past the end: at the end)
            out.append((loop_len, [0x80 | c, note, 0]))
        tracks.append(track(out, name))
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), 96) + b"".join(tracks)


def check(mid):
    """the file read back as a MIDI reader would: every chunk's length, every event whole; the note-ons counted"""
    assert mid[:4] == b"MThd" and struct.unpack(">I", mid[4:8])[0] == 6
    fmt, ntr, div = struct.unpack(">HHH", mid[8:14])
    o, ons, offs = 14, 0, 0
    for _ in range(ntr):
        assert mid[o:o + 4] == b"MTrk"
        n = struct.unpack(">I", mid[o + 4:o + 8])[0]
        t, end = o + 8, o + 8 + n
        while t < end:
            while mid[t] & 0x80:                  # the delta
                t += 1
            t += 1
            st = mid[t]
            if st == 0xFF:
                ln = mid[t + 2]
                last = mid[t + 1] == 0x2F
                t += 3 + ln
                if last:
                    assert t == end, "events after the end of a track"
            else:
                assert st & 0xF0 in (0x80, 0x90), "an event that is not a note"
                if st & 0xF0 == 0x90 and mid[t + 2]:
                    ons += 1
                else:
                    offs += 1
                t += 3
        assert t == end, "a track's length is not its events'"
        o = end
    assert o == len(mid)
    return fmt, ntr, div, ons, offs


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    meta, loop_len, events = read(open(sys.argv[1], "rb").read())
    mid = to_mid(meta, loop_len, events)
    out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].rsplit(".", 1)[0] + ".mid"
    open(out, "wb").write(mid)
    fmt, ntr, div, ons, offs = check(mid)
    if ons != offs:
        sys.exit(f"h1 loop: {ons} note-ons but {offs} note-offs")
    print(f"h1 loop: {ons} notes, {ntr - 1} tracks, {loop_len} ticks at {meta[1]} BPM -> {out} ({len(mid)} bytes)")


if __name__ == "__main__":
    main()
