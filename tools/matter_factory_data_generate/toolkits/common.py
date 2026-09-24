#!/usr/bin/env python3
import re

s_base_addr = 0
SZ_16M = 0x1000000
CRC_SIZE = 0X02
CRC_PAKEIT = 0X20

def p2v(addr):
    return (addr - s_base_addr)


def hex2int(str):
    return int(str, base=16) % (2**32)


def decimal2int(str):
    return int(str, base=10) % (2**32)


def crc_size(size):
    return ((size >> 5) * 34)


def crc_addr(addr):
    return crc_size(addr)


def size2int(str):
    size_str = re.findall(r"\d+", str)
    size = decimal2int(size_str[0])

    unit = re.findall(r"[k|K|m|M|g|G|b|B]+", str)
    if (unit[0] == 'b') or (unit[0] == 'B'):
        return size
    if (unit[0] == 'k') or (unit[0] == 'K'):
        return size * (1 << 10)
    elif (unit[0] == 'm') or (unit[0] == 'M'):
        return size * (1 << 20)
    elif (unit[0] == 'g') or (unit[0] == 'G'):
        return size * (1 << 30)
    else:
        print(f'invalid size unit {unit[0]}, must be "b/B/k/K/m/M/g/G"')


def is_out_of_range(addr, size):
    if ((addr + size) >= SZ_16M):
        return True
    return False

