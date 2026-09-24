#! /bin/sh

# config asia api in ./toolkits/merge.py

python main.py generate_from_asia \
    --cd ./asia_test_cd.bin \
    --vendor_id 0xfff1 \
    --product_id 0x1235 \
    -o factory_data_asia.bin
