#! /bin/sh
#
# gen.sh
# Copyright (C) 2023 frey <frey@frey-ubuntu1>
#
# Distributed under terms of the MIT license.
#

#python main.py generate --cd ./cd.bin --pai ./bk7231_pai.der --dac ./Chip-DAC-Cert-01.der --dac_key ./Chip-DAC-Key-01.der --discriminator 0xF01 --iteration_count 1000 --salt U1BBS0UyUCBLZXkgU2FsdA== --verifier uWFwqugDNGiEck/po7KHwwMwwqZgN10XuyBajPGuyzUEV/iree4lOrao5GuwnlQ65CJzbeUB49s31EH+NEkg0JVI5MGCQGMMT/SRPFNRODm3wH/MBiehuFc6FJ/NH6Rmzw== --pin_code 20202021 --vendor_name BEKEN --vendor_id 0x1342 --product_name lighting --product_id 0x03e8 --serialnum 123456 --unique_id 00112233445566778899aabbccddeeff -o factory_data.bin

####################################################
#Attention:
#Attention:
#You can change the file name after DAC and dac_key to specify the DAC_Cert and DAC-key you want.
###################################################
python main.py generate \
    --cd ./cd.bin \
    --pai ./Chip-PAI-Cert.der \
    --dac ./Chip-DAC-Cert_01.der \
    --dac_key ./Chip-DAC-Key_01.der \
    --vendor_name BEKEN \
    --vendor_id 0x1342 \
    --product_name lighting \
    --product_id 0x03ed \
    --serialnum  "BK000001" \
    --hardware_ver 0x1 \
    --hardware_ver_str "1.0" \
    -o qlfactory_data.bin
