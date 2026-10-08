"""Bounded, deterministic byte boundaries for local network parser harnesses.

Original protocol fixtures remain controls. Mutations are round-robin across
fixtures so a large certificate or packet cannot consume the whole budget.
These bytes are only fed to hosted parser lifts, never sent over a network.
"""


import itertools


def variants(data, max_len):
    size = len(data)
    cuts = set(range(min(size, 64)))
    cuts.update(range(max(0, size - 16), size))
    cuts.update(range(size) if size <= 512 else
                (size * i // 64 for i in range(1, 64)))
    for bit in range(max_len.bit_length()):
        cuts.update((2 ** bit - 1, 2 ** bit, 2 ** bit + 1))
    ordered = list(dict.fromkeys([size - 1, 0, size // 2] + sorted(cuts)))
    truncations = (data[:cut] for cut in ordered if 0 <= cut < size and cut <= max_len)
    suffixes = (data + suffix for suffix in
                (b"\x00", b"\xff", b"\x00\xff", b"\x00" * 16)
                if size + len(suffix) <= max_len)
    # Byte and multi-byte length boundaries at header and trailer positions.
    offsets = sorted(set(range(min(size, 64))) |
                     set(range(max(0, size - 8), size)))
    def replacements(width):
        for offset in offsets:
            if offset + width > size or size > max_len:
                continue
            maximum = (1 << (width * 8)) - 1
            values = dict.fromkeys((maximum, 0, maximum // 2, maximum // 2 + 1,
                                   min(size, maximum), min(size + 1, maximum),
                                   1, 127, 128, 255, min(256, maximum), maximum - 1))
            for value in values:
                for order in ("big", "little") if width > 1 else ("big",):
                    yield (data[:offset] + value.to_bytes(width, order) +
                           data[offset + width:])

    def integer_cases():
        # Spread the early budget across integer widths rather than exhausting
        # all byte values before reaching a multi-byte length/counter field.
        for row in itertools.zip_longest(*(replacements(width) for width in (1, 2, 4, 8))):
            for case in row:
                if case is not None:
                    yield case

    for row in itertools.zip_longest(truncations, suffixes, integer_cases()):
        for case in row:
            if case is not None:
                yield case


def expand(seeds, max_len, limit=4096, byte_limit=64 * 1024 * 1024):
    """Return originals plus at most limit unique variants and their census.

    The byte cap applies to generated data; original fixtures are never
    removed. Cap exhaustion is recorded rather than called exhaustive.
    """
    expanded = dict(seeds)
    seen = set(seeds.values())
    generators = [iter(variants(data, max_len))
                  for _, data in sorted(seeds.items())]
    count = used = 0
    capped = False
    while generators:
        active = []
        for generator in generators:
            data = next(generator, None)
            if data is None:
                continue
            active.append(generator)
            if data in seen:
                continue
            if count >= limit or used + len(data) > byte_limit:
                capped = True
                break
            seen.add(data)
            count += 1
            used += len(data)
            name = "procedural_%06d.bin" % count
            while name in expanded:
                name = "_" + name
            expanded[name] = data
        if capped:
            break
        generators = active
    return expanded, dict(original_seeds=len(seeds), generated_seeds=count,
                         generated_bytes=used, capped=capped,
                         variant_limit=limit, byte_limit=byte_limit)
