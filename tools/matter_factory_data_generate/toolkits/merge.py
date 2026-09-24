#!/usr/bin/env python3
import os
import sys
from .common import *
import logging
import struct
import random
import string
import base64
from OpenSSL import crypto
from Crypto.Cipher import AES
import requests
import hashlib
import hmac
import base64
import time
import datetime
import os
from .spake2p import generate_verifier

FLASH_SPACE_USING = 0xFFFFFF55  #依据flash只能写零，不能写1的原则
FLASH_SPACE_MASK  = 0xFFFF5555
FLASH_BLOCK_COUNT = 1
DAC_PRIV_ENCRYPT = 1
AES_KEY = b"\xae\x68\x52\xf8\x12\x10\x67\xcc\x4b\xf7\xa5\x76\x55\x77\xf3\x9e"
AES_NONCE = b"\x00\x00\x00\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00"
MAGIC_CODE = 0xF5F50000

# config for TrustAsia API
TRUSTASIA_API_NAME = "test"
TRUSTASIA_API_SECRET = "5abcb01c-046c-431f-aac7-27ebca046968"
TRUSTASIA_API_EXPIRE = 500
TRUSTASIA_API_HOST = "58.32.11.30"
TRUSTASIA_API_PORT = 8088
TRUSTASIA_API_URL = f"http://{TRUSTASIA_API_HOST}:{TRUSTASIA_API_PORT}/openapi/v1/factorydata"


def dac_encrypt(dac):
    cipher = AES.new(AES_KEY, AES.MODE_CCM, nonce=AES_NONCE)
    ct = cipher.encrypt(dac)
    return ct

def download_from_asia(vid, pid):
    def sign(timestamp, expire):
        param = f"keyname={TRUSTASIA_API_NAME}&timestamp={timestamp}&expire={expire}"
        signature = hmac.new(bytes(TRUSTASIA_API_SECRET, encoding="utf8"), bytes(param, encoding="utf8"), hashlib.sha256).hexdigest()
        return signature

    def download(vendorId, productId):
        timestamp = int(time.time())
        signature = sign(timestamp, TRUSTASIA_API_EXPIRE)
        params = {'keyname': TRUSTASIA_API_NAME, "timestamp": timestamp, "expire": TRUSTASIA_API_EXPIRE, "vendorId": hex(vendorId)[2:].upper(),
                "productId": hex(productId)[2:].upper(), "signature": signature}
        #print(f"url:{TRUSTASIA_API_URL}")
        #print(f"parames:{params}")
        response = requests.get(TRUSTASIA_API_URL, params=params)
        if not response.ok:
            print(f'download cert error code:{response.status_code}')
            sys.exit(1)
        return response.json()
    data = download(vid, pid)
    if data["code"] != 0:
        print(f'download cert error code:{data["code"]}, message:{data["error"]}')
        sys.exit(1)
    return data["data"]


FACTORY_DATA = {
    
    # CommissionableDataProvider
    'cert-dclrn': {
        'type': 'file',
        'encoding': 'binary',
        'value': None,
    },
    'firmware-infomation': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'pai-cert': {
        'type': 'file',
        'encoding': 'binary',
        'value': None,
    },
    'dac-cert': {
        'type': 'file',
        'encoding': 'binary',
        'value': None,
    },
    'dac-key': {
        'type': 'file',
        'encoding': 'binary',
        'value': None,
    },
    'dac-pub-key': {
        'type': 'file',
        'encoding': 'binary',
        'value': None,
    },
    

    # CommissionableDataProvider
    'discriminator': {
        'type': 'data',
        'encoding': 'u16',
        'value': None,
    },
    'iteration-count': {
        'type': 'data',
        'encoding': 'u32',
        'value': None,
    },
    'salt': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'verifier': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'pin-code': {
        'type': 'data',
        'encoding': 'u32',
        'value': None,
    },

    # DeviceInstanceInforProvider
    'vendor-id': {
        'type': 'data',
        'encoding': 'u16',
        'value': None,
    },
    'vendor-name': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'product-id': {
        'type': 'data',
        'encoding': 'u16',
        'value': None,
    },
    'product-name': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'serial-num': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'mfg-date': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'hardware-ver': {
        'type': 'data',
        'encoding': 'u16',
        'value': None,
    },
    'hardware-ver-str': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'uniqueId': {
        'type': 'data',
        'encoding': 'hex2bin',
        'value': None,
    },
    'partnumber': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'producturl': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },
    'productlabel': {
        'type': 'data',
        'encoding': 'string',
        'value': None,
    },

    # Other device info provider keys are dynamically generated
    # in the respective functions.
}

def check_str_range(s, min_len, max_len, name):
    if s and ((len(s) < min_len) or (len(s) > max_len)):
        logging.error('%s must be between %d and %d characters', name, min_len, max_len)
        sys.exit(1)

def check_int_range(value, min_value, max_value, name):
    if value and ((value < min_value) or (value > max_value)):
        logging.error('%s is out of range, should be in range [%d, %d]', name, min_value, max_value)
        sys.exit(1)

class images:

    def __init__(self, cd,pai,dac,dac_key,discriminator,iteration_count,salt,verifier,pin_code,vendor_name,product_name,vendor_id,product_id,serialnum,mfg_date,hardware_ver,hardware_ver_str,unique_id, data_from_asia, factory_data_v2, outfile):
        self.cd = cd
        self.pai = pai
        self.dac = dac
        self.dac_key = dac_key
        self.discriminator = discriminator
        self.iteration_count = iteration_count
        self.salt = salt
        self.verifier = verifier
        self.pin_code = pin_code
        self.vendor_name = vendor_name
        self.product_name = product_name
        self.vendor_id = vendor_id
        self.product_id = product_id
        self.serialnum = serialnum
        self.mfg_date = mfg_date
        self.hardware_ver = hardware_ver
        self.hardware_ver_str = hardware_ver_str
        self.unique_id = unique_id
        self.data_from_asia = data_from_asia
        self.outfile = outfile

        if(self.data_from_asia):
            self.get_data_from_asia()
        self.gen_default_value()

        check_int_range(discriminator, 0x0000, 0x0FFF, 'Discriminator')
        check_int_range(product_id, 0x0000, 0xFFFF, 'Product id')
        check_int_range(vendor_id, 0x0000, 0xFFFF, 'Vendor id')
        check_int_range(hardware_ver, 0x0000, 0xFFFF, 'Hardware version')

        check_str_range(serialnum, 1, 32, 'Serial number')
        check_str_range(vendor_name, 1, 32, 'Vendor name')
        check_str_range(product_name, 1, 32, 'Product name')
        check_str_range(hardware_ver_str, 1, 64, 'Hardware version string')
        check_str_range(mfg_date, 8, 16, 'Manufacturing date')
        check_str_range(unique_id, 16, 32, 'Rotating device Unique id')

        logging.info('Discriminator:{} '.format(discriminator))

        self.populate_factory_data()
        if factory_data_v2:
            self.generate_bin_all_v2()
        else:
            self.generate_bin()

    def get_data_from_asia(self):
        # in API:keyname,timestamp,expire,signature,token
        # in config :vendor_id, product_id
        # out : vendor_id,product_id
        # out : hw_ver,ver
        # out : discriminator,passcode,spake2p_iterations,spake2p_salt,spake2p_verifier,pai_cert,cert,private_key
        data = download_from_asia(self.vendor_id,self.product_id)
        self.discriminator = data["discriminator"]
        self.pin_code = data["passcode"]
        self.iteration_count = data["spake2p_iterations"]
        self.salt = data["spake2p_salt"]
        self.verifier = data["spake2p_verifier"]
        self.pai = data["pai_cert"]
        self.dac = data["cert"]
        self.dac_key = data["private_key"]

    def gen_default_value(self):
        if self.discriminator is None:
            self.discriminator = random.randint(0, 0xFFF);
        if self.salt is None:
            self.salt = base64.encodebytes(''.join(random.choice(string.ascii_letters + string.digits) for _ in range(16)).encode("utf-8")).decode("utf-8")
        if self.iteration_count is None:
            self.iteration_count = 1000
        if self.pin_code is None:
            self.pin_code = random.randint(1, 99999998)
        if self.verifier is None:
            verifier = generate_verifier(self.pin_code, base64.b64decode(self.salt), self.iteration_count)
            self.verifier = base64.b64encode(verifier).decode('ascii')

        if self.serialnum is None:
            self.serialnum = ''.join(random.choice(string.ascii_letters) for _ in range(16))
        if self.unique_id is None:
            self.unique_id = ''.join(random.choice(string.digits+'abcedf') for _ in range(16))
        if self.mfg_date is None:
            self.mfg_date = '2024-01-01'
        if self.hardware_ver is None:
            self.hardware_ver = 1
        if self.hardware_ver_str is None:
            self.hardware_ver_str = '1.0.0'

    def populate_factory_data(self):
        FACTORY_DATA['cert-dclrn']['value'] = os.path.abspath(self.cd)
        FACTORY_DATA['pai-cert']['value'] = os.path.abspath(self.pai)
        FACTORY_DATA['dac-cert']['value'] = os.path.abspath(self.dac)
        FACTORY_DATA['dac-key']['value'] = os.path.abspath(self.dac_key)

        FACTORY_DATA['discriminator']['value'] = self.discriminator
        FACTORY_DATA['iteration-count']['value'] = self.iteration_count
        FACTORY_DATA['salt']['value'] = self.salt
        FACTORY_DATA['verifier']['value'] = self.verifier
        FACTORY_DATA['pin-code']['value'] = self.pin_code
        
        if self.serialnum is not None:
            FACTORY_DATA['serial-num']['value'] = self.serialnum
        if self.unique_id is not None:
            FACTORY_DATA['uniqueId']['value'] = self.unique_id
        if self.mfg_date is not None:
            FACTORY_DATA['mfg-date']['value'] = self.mfg_date
        if self.vendor_id is not None:
            FACTORY_DATA['vendor-id']['value'] = self.vendor_id
        if self.vendor_name is not None:
            FACTORY_DATA['vendor-name']['value'] = self.vendor_name
        if self.product_id is not None:
            FACTORY_DATA['product-id']['value'] = self.product_id
        if self.product_name is not None:
            FACTORY_DATA['product-name']['value'] = self.product_name
        if self.hardware_ver is not None:
            FACTORY_DATA['hardware-ver']['value'] = self.hardware_ver
        if (self.hardware_ver_str is not None):
            FACTORY_DATA['hardware-ver-str']['value'] = self.hardware_ver_str
        FACTORY_DATA['firmware-infomation']['value'] = "FIRMWARE"
        FACTORY_DATA['partnumber']['value'] = "PART NUMBER"
        FACTORY_DATA['producturl']['value'] = "PRODUCT URL"
        FACTORY_DATA['productlabel']['value'] = "PRODUCT LABEL"

    def generate_bin(self):
        f = open(self.outfile, "wb")
        #1.bk_set_namespace
        dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
        dwOffsetByByte = struct.pack('<I', 0x1000)
        current_version = b'chip-factory'
        ucNamespace = struct.pack('56s', current_version)

        pad_buf = []
        for i in range(0x1000 - 64):
            pad_buf.append(0xff)
        pad_buf_bytes = bytes(pad_buf)

        content = dwUsingFlag + dwOffsetByByte + ucNamespace + pad_buf_bytes

        #2.bk_set_name
        i_name_index = 0
        area_name=b''
        total_len = 0
        for k, v in FACTORY_DATA.items():
            if k == 'cert-dclrn':
                infile_len = os.path.getsize(self.cd)
                dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                dwOffsetByByte = struct.pack('<I', len(area_name))
                dwlength = struct.pack('<I', infile_len)
                ucName = struct.pack('52s', k.encode())
                area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName
                total_len += infile_len
            elif k == 'pai-cert':
                infile_len = 0
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.pai)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                    infile_len = len(bytes_obj)
                elif(os.path.splitext(self.pai)[1].lower() == ".pem"):
                    with open(self.pai, "rb") as fs:
                        bytes_obj = fs.read()
                        bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                        bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                        infile_len = len(bytes_obj)
                elif(os.path.splitext(self.pai)[1].lower() == ".der"):
                    infile_len = os.path.getsize(self.pai)
                else:
                    print(f"pai {self.pai} error")
                    sys.exit(1)
                dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                dwOffsetByByte = struct.pack('<I', total_len)
                dwlength = struct.pack('<I', infile_len)
                ucName = struct.pack('52s', k.encode())
                area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName
                total_len += infile_len
            elif k == 'dac-cert':
                infile_len = 0
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.dac)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                    infile_len = len(bytes_obj)
                elif(os.path.splitext(self.dac)[1].lower() == ".pem"):
                    with open(self.dac, "rb") as fs:
                        bytes_obj = fs.read()
                        bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                        bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                        infile_len = len(bytes_obj)
                elif(os.path.splitext(self.pai)[1].lower() == ".der"):
                    infile_len = os.path.getsize(self.dac)
                else:
                    print(f"dac-cert {self.dac} error")
                    sys.exit(1)
                dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                dwOffsetByByte = struct.pack('<I', total_len)
                dwlength = struct.pack('<I', infile_len)
                ucName = struct.pack('52s', k.encode())
                area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName
                total_len += infile_len
            elif k == 'dac-key':
                #1.calc dac-key value
                dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                dwOffsetByByte = struct.pack('<I', total_len)
                dwlength = struct.pack('<I', 32)
                ucName = struct.pack('52s', k.encode())
                area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName
                total_len += 32
                #2.calc dac-pub-key value
                dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                dwOffsetByByte = struct.pack('<I', total_len)
                dwlength = struct.pack('<I', 65)

                current_dac = b'dac-pub-key'
                ucName = struct.pack('52s', current_dac)
                area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName
                total_len += 65
            else:
                if v['value'] is not None:
                    dwUsingFlag = struct.pack('<I', FLASH_SPACE_USING)
                    dwOffsetByByte = struct.pack('<I', total_len)
                    dwlength = b""
                    if v['encoding'] == 'string' or v['encoding'] == 'hex2bin':
                        dwlength = struct.pack('<I', len(v['value']))
                        total_len += len(v['value'])
                    elif v['encoding'] == 'u16' :
                        dwlength = struct.pack('<I', 2)
                        total_len += 2
                    elif v['encoding'] == 'u32':
                        dwlength = struct.pack('<I', 4)
                        total_len += 4

                    ucName = struct.pack('52s', k.encode())
                    area_name += dwUsingFlag + dwOffsetByByte + dwlength + ucName

            i_name_index += 1

        #fill 0xff
        pad_buf = []
        for _ in range(0x1000 * FLASH_BLOCK_COUNT  - len(area_name)):
            pad_buf.append(0xff)
        pad_buf_bytes = bytes(pad_buf)

        content += area_name + pad_buf_bytes

        #3.bk_set_data
        i_name_index = 0
        area_data=b''
        bytes_obj=b''
        for k, v in FACTORY_DATA.items():
            if k == 'cert-dclrn':
                with open(self.cd, "rb") as fs:
                    bytes_obj = fs.read()
                    area_data += bytes_obj
            elif k == 'pai-cert':
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.pai)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.pai, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.pai)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                area_data += bytes_obj
            elif k == 'dac-cert':
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.dac)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.dac, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.dac)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                area_data += bytes_obj
            elif k == 'dac-key':
                if self.data_from_asia:
                    bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, self.dac_key)
                    bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.dac_key, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.dac_key)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                #1.calc dac-key value
                bytes_dac_key = bytes_obj[7:32 + 7]
                if DAC_PRIV_ENCRYPT==1:
                    bytes_dac_key = dac_encrypt(bytes_dac_key)
                #2.calc dac-pub-key value
                bytes_dac_pub_key = bytes_obj[56:65 + 56]
                bytes_obj = bytes_dac_key + bytes_dac_pub_key
                area_data += bytes_obj
            else:
                if v['value'] is not None:
                    indata = b''
                    if v['encoding'] == 'string' or v['encoding'] == 'hex2bin':
                        indata = struct.pack("%ds" % (len(str(v['value'])),), str(v['value']).encode())
                    elif v['encoding'] == 'u16':
                        indata = struct.pack('<H', int(str(v['value']),base=10))
                    elif v['encoding'] == 'u32':
                        indata = struct.pack('<I', int(str(v['value']),base=10))
                    area_data += indata

            i_name_index += 1

        content += area_data

        pad_buf = []
        for i in range(0x3000 - len(content)):
            pad_buf.append(0xff)
        pad_buf_bytes = bytes(pad_buf)
        content += pad_buf_bytes

        f.write(content)
        f.flush()
        f.close()

    def generate_bin_v2(self, data_list, outfile):
        data_offset = [0]
        area_data = b''
        for k in data_list:
            v = FACTORY_DATA[k]
            if k == 'cert-dclrn':
                with open(self.cd, "rb") as fs:
                    bytes_obj = fs.read()
                    area_data += bytes_obj
                    data_offset.append(data_offset[-1] + len(bytes_obj))
            elif k == 'pai-cert':
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.pai)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.pai, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.pai)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                area_data += bytes_obj
                data_offset.append(data_offset[-1] + len(bytes_obj))
            elif k == 'dac-cert':
                if self.data_from_asia:
                    bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, self.dac)
                    bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.dac, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.dac)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_certificate(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_certificate(crypto.FILETYPE_ASN1, bytes_obj)
                area_data += bytes_obj
                data_offset.append(data_offset[-1] + len(bytes_obj))
            elif k == 'dac-key':
                if self.data_from_asia:
                    bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, self.dac_key)
                    bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.dac_key, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.dac_key)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                #1.calc dac-key value
                bytes_dac_key = bytes_obj[7:32 + 7]
                if DAC_PRIV_ENCRYPT==1:
                    bytes_dac_key = dac_encrypt(bytes_dac_key)
                area_data += bytes_dac_key
                data_offset.append(data_offset[-1] + len(bytes_dac_key))
            elif k == 'dac-pub-key':
                if self.data_from_asia:
                    bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, self.dac_key)
                    bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                else:
                    with open(self.dac_key, "rb") as fs:
                        bytes_obj = fs.read()
                        if(os.path.splitext(self.dac_key)[1].lower() == ".pem"):
                            bytes_obj = crypto.load_privatekey(crypto.FILETYPE_PEM, bytes_obj)
                            bytes_obj = crypto.dump_privatekey(crypto.FILETYPE_ASN1, bytes_obj)
                #2.calc dac-pub-key value
                bytes_dac_pub_key = bytes_obj[56:65 + 56]
                area_data += bytes_dac_pub_key
                data_offset.append(data_offset[-1] + len(bytes_dac_pub_key))
            elif k == 'mfg-date':
                date = datetime.datetime.strptime(v['value'], "%Y-%M-%d")
                year, month, day = date.year, date.month, date.day
                indata = struct.pack('<I', (year<<16)+(month<<8)+day)
                area_data += indata
                data_offset.append(data_offset[-1] + len(indata))
            else:
                if v['value'] is not None:
                    indata = b''
                    if v['encoding'] == 'string' or v['encoding'] == 'hex2bin':
                        indata = struct.pack("%ds" % (len(str(v['value'])),), str(v['value']).encode())
                    elif v['encoding'] == 'u16':
                        indata = struct.pack('<H', int(str(v['value']),base=10))
                    elif v['encoding'] == 'u32':
                        indata = struct.pack('<I', int(str(v['value']),base=10))
                    area_data += indata
                    data_offset.append(data_offset[-1] + len(indata))
                else:
                    print(f'{k} error')

        with open(outfile, 'wb') as fp:
            fp.write(struct.pack('<I', MAGIC_CODE))
            fp.write(struct.pack('<I', 8*(len(data_list)+1)))
            for offset, n_offset in zip(data_offset[:-1], data_offset[1:]):
                fp.write(struct.pack('<I', offset))
                fp.write(struct.pack('<I', n_offset - offset))
            fp.write(area_data)

    def generate_bin_all_v2(self):
        cert_list = ['cert-dclrn', 'firmware-infomation',    'dac-cert', 'pai-cert',
                     'dac-pub-key', 'dac-key']
        other_list = ['discriminator',    'iteration-count',
                     'salt', 'verifier',   'pin-code', 'vendor-name',
                     'vendor-id', 'product-name', 'product-id', 'serial-num',
                     'mfg-date', 'hardware-ver', 'hardware-ver-str', 'uniqueId',
                     'partnumber', 'producturl', 'productlabel']
        cert_file_name = f'cert.{self.outfile}'
        other_file_name = f'other.{self.outfile}'
        self.generate_bin_v2(cert_list, cert_file_name)
        self.generate_bin_v2(other_list, other_file_name)
        with open(cert_file_name, 'rb') as fp:
            cert_data = fp.read( 4 * 1024 )
        with open(other_file_name, 'rb') as fp:
            other_data = fp.read( 4 * 1024 )
        cert_data = cert_data + b'\xFF' * (4 * 1024 - len(cert_data))
        with open(self.outfile, 'wb') as fp:
            fp.write(cert_data)
            fp.write(other_data)
