#!/usr/bin/env python3

from argparse import _MutuallyExclusiveGroup
from genericpath import exists
import click
from click_option_group import optgroup,RequiredMutuallyExclusiveOptionGroup

import struct
from toolkits import images

def any_base_int(s): return int(s, 0)

@click.group()
@click.version_option(version='1.0.0.0')
def cli():
    """This script showcases multi-toolkits on operating flash."""
    pass

@cli.command("generate")
@click.option( "--cd",  type=click.Path(exists=True, dir_okay=False),required=True, help="The path to the certificate declaration der format.")
@click.option( "--pai",  type=click.Path(exists=True, dir_okay=False),required=True, help="The path to the PAI certificate in der format.")
@click.option( "--dac",  type=click.Path(exists=True, dir_okay=False),required=True, help="The path to the DAC certificate in der format.")
@click.option( "--dac_key",  type=click.Path(exists=True, dir_okay=False),required=True, help="The path to the DAC private key in der format.")
@click.option( "--discriminator",  type=any_base_int, help="The discriminator for pairing, range: 0x00-0x0FFF.")
@click.option( "--iteration_count",  type=any_base_int, help="The iteration-count value.")
@click.option( "--salt",  type=str, help="The salt value.")
@click.option( "--verifier",  type=str, help="The verifier value.")
@click.option( "--pin_code",  type=any_base_int, help="The pin-code value.")
@click.option("--vendor_name",  type=str, default="TEST", help="The vendor-name value.", show_default=True)
@click.option("--vendor_id",  type=any_base_int, default="0x1342", help="The vendor-id value.", show_default=True)
@click.option("--product_name",  type=str, default="TEST_PRODUCT", help="The product-name value.", show_default=True)
@click.option("--product_id",  type=any_base_int, default="0x3E8", help="The product-id value.", show_default=True)
@click.option( "--serialnum",  type=str, help="The SerialNum value.")
@click.option( "--mfg_date",  type=str, help="Manufacturing date in format YYYY-MM-DD.")
@click.option( "--hardware_ver",  type=any_base_int, help="Hardware version.")
@click.option( "--hardware_ver_str",  type=str, help="Hardware version string.")
@click.option( "--unique_id",  type=str, help="uniqueId.")
@click.option( "--factory_data_v2",  type=bool, default=True, help="factory data v2")
@click.option("-o", "--outfile",  default="merge.bin", help="The outfile name.", show_default=True)
def generate_bin(cd,pai,dac,dac_key,discriminator,iteration_count,salt,verifier,pin_code,vendor_name,vendor_id,product_name,product_id,serialnum,mfg_date,hardware_ver,hardware_ver_str,unique_id,factory_data_v2, outfile):
    """generate a new bin file from certificate files and some params."""
    images(cd,pai,dac,dac_key,discriminator,iteration_count,salt,verifier,pin_code,vendor_name,product_name,vendor_id,product_id,serialnum,mfg_date,hardware_ver,hardware_ver_str,unique_id, False, factory_data_v2, outfile)

@cli.command("generate_from_asia")
@click.option( "--cd",  type=click.Path(exists=True, dir_okay=False),required=True, help="The path to the certificate declaration der format.")
@click.option( "--vendor_name",  type=str, default="TEST", help="The vendor-name value.", show_default=True)
@click.option( "--vendor_id",  type=any_base_int, default="0x1342", help="The vendor-id value.", show_default=True)
@click.option( "--product_name",  type=str, default="TEST_PRODUCT", help="The product-name value.",show_default=True)
@click.option( "--product_id",  type=any_base_int, default="0x3E8", help="The product-id value.",show_default=True)
@click.option( "--serialnum",  type=str, help="The SerialNum value.")
@click.option( "--mfg_date",  type=str, help="Manufacturing date in format YYYY-MM-DD.")
@click.option( "--hardware_ver",  type=any_base_int, help="Hardware version.")
@click.option( "--hardware_ver_str",  type=str, help="Hardware version string.")
@click.option( "--unique_id",  type=str, help="uniqueId.")
@click.option( "--factory_data_v2",  type=bool, default=True, help="factory data v2")
@click.option("-o", "--outfile",  default="merge.bin", help="The outfile name.", show_default=True)
def generate_bin_from_asia(cd,vendor_name,vendor_id,product_name,product_id,serialnum,mfg_date,hardware_ver,hardware_ver_str,unique_id,factory_data_v2, outfile):
    """generate a new bin file from certificate files and some params."""
    images(cd,None,None,None,None,None,None,None,None,vendor_name,product_name,vendor_id,product_id,serialnum,mfg_date,hardware_ver,hardware_ver_str,unique_id, True, factory_data_v2, outfile)



if __name__ == '__main__':
    cli()
