import sys, zlib, hashlib


def forge_suffix(data: bytes, target: int) -> bytes:
    zero = bytes(4)
    base = zlib.crc32(data + zero) & 0xFFFFFFFF
    deltas = []
    for i in range(32):
        p = bytearray(4)
        p[i // 8] = 1 << (i % 8)
        c = zlib.crc32(data + bytes(p)) & 0xFFFFFFFF
        deltas.append(c ^ base)

    rhs = target ^ base
    rows = []
    for bit in range(32):
        coeff = 0
        for var in range(32):
            if (deltas[var] >> bit) & 1:
                coeff |= 1 << var
        if (rhs >> bit) & 1:
            coeff |= 1 << 32
        rows.append(coeff)

    pivot_row = 0
    pivot_for_col = [-1] * 32
    for col in range(32):
        pivot = None
        for r in range(pivot_row, 32):
            if (rows[r] >> col) & 1:
                pivot = r
                break
        if pivot is None:
            continue
        rows[pivot_row], rows[pivot] = rows[pivot], rows[pivot_row]
        for r in range(32):
            if r != pivot_row and ((rows[r] >> col) & 1):
                rows[r] ^= rows[pivot_row]
        pivot_for_col[col] = pivot_row
        pivot_row += 1

    if pivot_row != 32:
        raise RuntimeError(f"CRC matrix rank {pivot_row}, expected 32")

    solution = 0
    for col in range(32):
        r = pivot_for_col[col]
        if r < 0:
            raise RuntimeError("Missing pivot")
        if (rows[r] >> 32) & 1:
            solution |= 1 << col

    patch = solution.to_bytes(4, "little")
    final_crc = zlib.crc32(data + patch) & 0xFFFFFFFF
    if final_crc != target:
        raise RuntimeError(f"CRC forge failed: {final_crc:08X} != {target:08X}")
    return patch


def main():
    if len(sys.argv) != 3:
        print("usage: forge_crc.py <file> <target_crc_hex>")
        raise SystemExit(2)
    path = sys.argv[1]
    target = int(sys.argv[2], 16)
    with open(path, "rb") as f:
        data = f.read()
    suffix = forge_suffix(data, target)
    with open(path, "ab") as f:
        f.write(suffix)
    with open(path, "rb") as f:
        final = f.read()
    crc = zlib.crc32(final) & 0xFFFFFFFF
    sha = hashlib.sha256(final).hexdigest()
    print(f"patched={path}")
    print(f"suffix={suffix.hex().upper()}")
    print(f"crc32={crc:08X}")
    print(f"sha256={sha}")
    print(f"size={len(final)}")

if __name__ == "__main__":
    main()
