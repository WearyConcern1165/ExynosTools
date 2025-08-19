// Utilities for BC4/5 decode

// Unpack 3-bit indices from 48-bit stream (stored as lo32 in bits.y, hi16 in bits.x)
uint bc3bit_index(uvec2 bits, int i) {
    int bitpos = i * 3;
    if (bitpos < 32) {
        return (bits.y >> bitpos) & 0x7u;
    } else {
        int off = bitpos - 32;
        return (bits.x >> off) & 0x7u;
    }
}

void bc4_palette(uint r0, uint r1, out uint pal[8]) {
    pal[0] = r0;
    pal[1] = r1;
    if (r0 > r1) {
        pal[2] = (6u * r0 + 1u * r1) / 7u;
        pal[3] = (5u * r0 + 2u * r1) / 7u;
        pal[4] = (4u * r0 + 3u * r1) / 7u;
        pal[5] = (3u * r0 + 4u * r1) / 7u;
        pal[6] = (2u * r0 + 5u * r1) / 7u;
        pal[7] = (1u * r0 + 6u * r1) / 7u;
    } else {
        pal[2] = (4u * r0 + 1u * r1) / 5u;
        pal[3] = (3u * r0 + 2u * r1) / 5u;
        pal[4] = (2u * r0 + 3u * r1) / 5u;
        pal[5] = (1u * r0 + 4u * r1) / 5u;
        pal[6] = 0u;
        pal[7] = 255u;
    }
}

