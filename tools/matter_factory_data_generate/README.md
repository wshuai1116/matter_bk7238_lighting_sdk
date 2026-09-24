# qingdao_cert_generate_bin

## 将工程配置成使用私有配置信息的设备

在 connectedhomeip/src/platform/Beken/CHIPDevicePlatformConfig.h 中将 CONFIG_ENABLE_BEKEN_DEVICE_INFO 配置为 1。 如下所示：

	#define  CONFIG_ENABLE_BEKEN_DEVICE_INFO  1

**注意：如果 CONFIG_ENABLE_BEKEN_DEVICE_INFO 为0，表示设备使用Matter协议默认的配置信息。**

## 生成factory_data.bin的步骤

在工程的 toosls/matter_factory_data_generate 文件夹下配置私有设备的信息。通过配置.gen.sh脚本，以便生成想要的bin文件。

	pyhton main.py generate --cd cd.bin --pai Chip-PAI-Cert.der --dac Chip-DAC-Cert-01.der --dac_key Chip-DAC-Key-01.der  --vendor_name BEKEN --vendor_id 0x1342 --product_name lighting --product_id 0x3ed  -o factory_data.bin

在 toosls/matter_factory_data_generate 文件夹中提供了三对儿DAC和DAC_KEY，dac和dac_key需要成对使用。如有需要，可以使用PAI和PAI_KEY再生成更多的DAC和DAC_KEY。可以灵活设置 vendor_name、product_name、serial-num、hardware-ver等参数。参数配置完成后，运行./gen.sh 即可生成factory_data.bin。

**注意：此路径中提供的PAI、DAC和CD证书都是基于 vendor_id 为0x1342，product_id 为0x03ed生成的，这两个参数不要修改。除非根据私有的 vendor_id 和 product_id 重新签名了PAI、DAC和CD证书。**

## 烧写factory_data.bin

在bk_loder的工具中选中factory_data.bin之所在的位置，设置factory_data.bin在Flash中的起始地址。如果设备是BK7236,factory_data.bin在Flash中的起始地址,为vendor_flash.c中全局变量bk_flash_partiton中matter_factory分区的起始地址。如果设备是BK7231n或者BK7238，factory_data.bin在Flash中的起始地址,为BkDriverFlash.c中全局变量bk7231_partiton_2M（Flash为2M大小）或者bk7231_partiton_4M（Flash为4M大小）matter factory info分区的起始地址。

# dac private key encrypt

config in file `./toolkits/merge.py`

```
DAC_PRIV_ENCRYPT = 1 # encrypt dac private
AES_KEY = b"\xae\x68\x52\xf8\x12\x10\x67\xcc\x4b\xf7\xa5\x76\x55\x77\xf3\x9e" # aes key
AES_NONCE = b"\x00\x00\x00\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00" # aes nonce
```
