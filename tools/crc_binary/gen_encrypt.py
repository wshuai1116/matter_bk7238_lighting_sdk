#! /usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:fenc=utf-8
#
# Copyright © 2023 frey <frey@frey-ubuntu1>
#
# Distributed under terms of the MIT license.

"""
bootloader: ./tools/beken_packager/bootloader_bk7231n_uart2_v1.0.12_efuse_no_partition.bin
bootloader_partition: ./tools/beken_packager/bootloader_bk7231n_uart2_v1.0.12.bin
bsp_bin: ./out/beken7231_bsp.bin
encrypt tool: ./tools/crc_binarycmake_encrypt_crc
"""
import os
import json
import sys

coef = [0x510fb093, 0xa3cbeadc, 0x5993a17e, 0xc7adeb03]
iv_table_a = b'0123456789ABCDEF'
key_table_a = b'0123456789ABCDEF0123456789ABCDEF'

def get_bootloader_partition():
    if len(sys.argv) == 1 or sys.argv[1] == '5':
        return os.path.abspath("./tools/beken_packager/bootloader_bk7231n_uart1_v1.0.13.bin");
    if sys.argv[1] == '7':
        return os.path.abspath("./tools/beken_packager/bootloader_bk7238_uart1_v1.0.14.bin");
    return ""

input_files = {
    "bootloader_partition": get_bootloader_partition(), # 有分区表bootloader, 用于将其分区表转移到加密后的bootloader中
    "bsp_bin": os.path.abspath("./out/bk7231_bsp.bin"), # 待加密的app bin
    "cmake_encrypt_crc": os.path.abspath("./tools/crc_binary/cmake_encrypt_crc"), # 加密工具
    "beken_packager": os.path.abspath("./tools/beken_packager/beken_packager") # 打包工具
}
out_files = {
    "bsp_bin_enc": os.path.abspath("./out/bk7231_bsp_enc.bin"), # 加密后app bin
    "bootloader": os.path.abspath("./out/bootloader_efuse_no_partition.bin"), # 无分区表待加密bootloader
    "bootloader_coef": os.path.abspath("./out/bootloader_efuse_no_partition_coef.bin"), # 写入coef
    "bootloader_coef_enc": os.path.abspath("./out/bootloader_efuse_no_partition_coef_enc.bin"), # 加密后无分区表
    "bootloader_coef_enc_pt": os.path.abspath("./out/bootloader_efuse_no_partition_coef_enc_pt.bin"), # 加密后有分区表
    "all_bin": os.path.abspath("./out/all_2M.1220.enc.bin"),
    "out_json": os.path.abspath("./out/tmp.json")
}
packager_json = {
    "magic": "RT-Thread",
    "version": "0.1",
    "count": 2,
    "section": [
        {
            "firmware": out_files["bootloader_coef_enc_pt"],
            "version": "2M.1220.enc",
            "partition": "bootloader",
            "start_addr": "0x00000000",
            "size": "65280"
        },
        {
            "firmware": out_files["bsp_bin_enc"],
            "version": "2M.1220.enc",
            "partition": "app",
            "start_addr": "0x00011000",
            "size": "1190K"
        }
    ]
}


def check_files():
    if get_bootloader_partition() == "":
        print(f"soc not support!!!")
        return False
    for _, file in input_files.items():
        if not os.path.exists(file):
            print(f"file {file} not exist!!!")
            return False
    for _, file in out_files.items():
        if os.path.exists(file):
            os.remove(file)
        if os.path.exists(file):
            print(f"file {file} exist, can't remove!!!")
            return False
    return True

def gen_all_bin():
    os.chdir(os.path.abspath("./out"))

    # ==================================================
    # encrypt bsp_bin
    cmd = "{} -enc {} {} {} {} {} -start-add 10000>/dev/null".format(
        input_files["cmake_encrypt_crc"],
        input_files["bsp_bin"],
        hex(coef[0])[2:],
        hex(coef[1])[2:],
        hex(coef[2])[2:],
        hex(coef[3])[2:]
    )
    os.system(cmd)
    if not os.path.exists(out_files["bsp_bin_enc"]):
        print("bsp_bin encrypt failed!!!")
        return
    # ==================================================
    # delete partition table in bootloader_partition, generate bootloader
    with open(input_files["bootloader_partition"],'rb') as fp:
        bootloader = fp.read(4*1024*1024)
    bootloader = list(bootloader)
    bootloader = bootloader[:0xd3b0]
    with open(out_files["bootloader"],'wb') as fp:
        fp.write(bytes(bootloader))
    if not os.path.exists(out_files["bootloader"]):
        print("bootloader failed!!!")
        return

    # ==================================================
    # write coef to bootloader, generate bootloader_coef
    with open(out_files["bootloader"],'rb') as fp:
        bootloader = fp.read(4*1024*1024)
    # 0x1f08 . 16 coef0-3
    bootloader = list(bootloader)
    replace = b''.join([c.to_bytes(4, 'little') for c in coef])
    for i in range(16):
        bootloader[0x1f08+i] = replace[i]
    # 0x1f1c . 16 iv_table_a
    for i in range(16):
        bootloader[0x1f1c+i] = iv_table_a[i]
    # 0x1f2d . 32 key_table_a
    for i in range(32):
        bootloader[0x1f2d+i] = key_table_a[i]
    with open(out_files["bootloader_coef"],'wb') as fp:
        fp.write(bytes(bootloader))
    if not os.path.exists(out_files["bootloader_coef"]):
        print("bootloader_coef failed!!!")
        return

    # =====================================================
    # encrypt bootloader_coef, generate bootloader_coef_enc
    cmd = "{} -enc {} {} {} {} {}>/dev/null".format(
        input_files["cmake_encrypt_crc"],
        out_files["bootloader_coef"],
        hex(coef[0])[2:],
        hex(coef[1])[2:],
        hex(coef[2])[2:],
        hex(coef[3])[2:]
    )
    os.system(cmd)
    if not os.path.exists(out_files["bootloader_coef_enc"]):
        print("bootloader_coef encrypt failed!!!")
        return

    # =====================================================
    # append partition info to bootloader_coef_enc, generate bootloader_coef_enc_pt
    # 0xd3b0
    with open(out_files["bootloader_coef_enc"],'rb') as fp:
        bootloader_coef_enc = list(fp.read(4*1024*1024))
    with open(input_files["bootloader_partition"],'rb') as fp:
        bootloader_partition = list(fp.read(4*1024*1024))
    for i in bootloader_partition[0xd3b0:]:
        bootloader_coef_enc.append(i)
    with open(out_files["bootloader_coef_enc_pt"],'wb') as fp:
        fp.write(bytes(bootloader_coef_enc))

    # =====================================================
    # combine bootloader_coef_enc_pt and bsp_bin_enc, generate all_bin
    with open(out_files["out_json"],'w') as fp:
        fp.write(json.dumps(packager_json, sort_keys=True, indent=4))
    cmd = "{} {}>/dev/null".format(
        input_files["beken_packager"],
        out_files["out_json"]
    )
    os.system(cmd)
    if not os.path.exists(out_files["all_bin"]):
        print("package all_bin failed!!!")
        return


if __name__ == "__main__":
    if check_files():
        gen_all_bin()
        
    

