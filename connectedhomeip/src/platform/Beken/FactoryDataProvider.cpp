/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <credentials/CHIPCert.h>
#include <crypto/CHIPCryptoPAL.h>
#include <crypto/DefaultSessionKeystore.h>
#include <lib/core/CHIPError.h>
#include <lib/support/Base64.h>
#include <lib/support/BytesToHex.h>
#include <lib/support/Span.h>
#include <platform/Beken/CHIPDevicePlatformConfig.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>
#include <platform/internal/GenericConfigurationManagerImpl.ipp>

#include <platform/Beken/BekenConfig.h>
#include <platform/Beken/FactoryDataProvider.h>
#include <platform/Beken/matter_pal.h>

using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

// TODO: This should be moved to a method of P256Keypair
CHIP_ERROR LoadKeypairFromRaw(ByteSpan private_key, ByteSpan public_key, Crypto::P256Keypair & keypair)
{
    Crypto::P256SerializedKeypair serialized_keypair;
    ReturnErrorOnFailure(serialized_keypair.SetLength(private_key.size() + public_key.size()));
    memcpy(serialized_keypair.Bytes(), public_key.data(), public_key.size());
    memcpy(serialized_keypair.Bytes() + public_key.size(), private_key.data(), private_key.size());
    return keypair.Deserialize(serialized_keypair);
}

CHIP_ERROR FactoryDataProvider::ReadCertDataHeader()
{
    if(mCertDataHeader.magic_code != 0)
        return CHIP_NO_ERROR;
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset,
                (uint8_t*)&mCertDataHeader,
                8),
            CHIP_ERROR_READ_FAILED);
    uint32_t header_length = mCertDataHeader.header_length;
    if(header_length > sizeof(mCertDataHeader))
    {
        header_length = sizeof(mCertDataHeader);
    }
    if(mCertDataHeader.magic_code < 0xF5F50000)
    {
        mCertDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    if(kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset,
                (uint8_t*)&mCertDataHeader,
                header_length))
    {
        mCertDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}
CHIP_ERROR FactoryDataProvider::ReadFlashDataHeader()
{
    if(mFlashDataHeader.magic_code != 0)
        return CHIP_NO_ERROR;
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset,
                (uint8_t*)&mFlashDataHeader,
                8),
            CHIP_ERROR_READ_FAILED);
    uint32_t header_length = mFlashDataHeader.header_length;
    if(header_length > sizeof(mFlashDataHeader))
    {
        header_length = sizeof(mFlashDataHeader);
    }
    if(mFlashDataHeader.magic_code < 0xF5F50000)
    {
        mFlashDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    if(kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset,
                (uint8_t*)&mFlashDataHeader,
                header_length))
    {
        mFlashDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}
CHIP_ERROR FactoryDataProvider::GetCertificationDeclaration(MutableByteSpan & outBuffer)
{
    ReturnErrorOnFailure(ReadCertDataHeader());
    ReturnErrorCodeIf(outBuffer.size() < mCertDataHeader.CertificationDeclaration.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset + mCertDataHeader.CertificationDeclaration.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.CertificationDeclaration.length),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.CertificationDeclaration.length);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetFirmwareInformation(MutableByteSpan & out_firmware_info_buffer)
{
    // TODO: We need a real example FirmwareInformation to be populated.
    out_firmware_info_buffer.reduce_size(0);

    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetDeviceAttestationCert(MutableByteSpan & outBuffer)
{
    ReturnErrorOnFailure(ReadCertDataHeader());
    ReturnErrorCodeIf(outBuffer.size() < mCertDataHeader.DeviceAttestationCert.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset + mCertDataHeader.DeviceAttestationCert.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.DeviceAttestationCert.length
                ),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.DeviceAttestationCert.length);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductAttestationIntermediateCert(MutableByteSpan & outBuffer)
{
    ReturnErrorOnFailure(ReadCertDataHeader());
    ReturnErrorCodeIf(outBuffer.size() < mCertDataHeader.ProductAttestationIntermediateCert.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset + mCertDataHeader.ProductAttestationIntermediateCert.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.ProductAttestationIntermediateCert.length
                ),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.ProductAttestationIntermediateCert.length);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SignWithDeviceAttestationKey(const ByteSpan & messageToSign, MutableByteSpan & outSignBuffer)
{
    size_t kDacPublicKeyLen = 0, kDacPrivateKeyLen = 0;
    uint8_t kEncryptDacPrivateKey[40] = { 0 }, kDacPublicKey[70] = { 0 }, kDacPrivateKey[40] = { 0 };
    ReturnErrorOnFailure(ReadCertDataHeader());
    ReturnErrorCodeIf(40 < mCertDataHeader.DacPrivateKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(70 < mCertDataHeader.DacPublicKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset + mCertDataHeader.DacPrivateKey.off_set + mCertDataHeader.header_length,
                kEncryptDacPrivateKey,
                mCertDataHeader.DacPrivateKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPrivateKeyLen = mCertDataHeader.DacPrivateKey.length;
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                certDataHeaderOffset + mCertDataHeader.DacPublicKey.off_set + mCertDataHeader.header_length,
                kDacPublicKey,
                mCertDataHeader.DacPublicKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPublicKeyLen = mCertDataHeader.DacPublicKey.length;
#if CONFIG_BEKEN_DAC_PRIV_ENCRYPT
    Crypto::Aes128KeyHandle key;
    Crypto::DefaultSessionKeystore keystore;
    Crypto::Symmetric128BitsKeyByteArray keyMaterial;
    memcpy(&keyMaterial, AESKey, AESKeyLen);
    keystore.CreateKey(keyMaterial, key);
    ReturnErrorOnFailure(Crypto::AES_CTR_crypt(kEncryptDacPrivateKey, kDacPrivateKeyLen, key, AESNonce, AESNonceLen, kDacPrivateKey));
    keystore.DestroyKey(key);
#else
    memcpy(kDacPrivateKey, kEncryptDacPrivateKey, kDacPrivateKeyLen);
#endif
    ReturnErrorCodeIf(70 == kDacPublicKeyLen, CHIP_ERROR_BUFFER_TOO_SMALL);

    Crypto::P256ECDSASignature signature;
    Crypto::P256Keypair keypair;

    VerifyOrReturnError(IsSpanUsable(outSignBuffer), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(IsSpanUsable(messageToSign), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(outSignBuffer.size() >= signature.Capacity(), CHIP_ERROR_BUFFER_TOO_SMALL);

    // In a non-exemplary implementation, the public key is not needed here. It is used here merely because
    // Crypto::P256Keypair is only (currently) constructable from raw keys if both private/public keys are present.
    ReturnErrorOnFailure( LoadKeypairFromRaw(ByteSpan(kDacPrivateKey, kDacPrivateKeyLen), ByteSpan(kDacPublicKey, kDacPublicKeyLen), keypair));
    ReturnErrorOnFailure(keypair.ECDSA_sign_msg(messageToSign.data(), messageToSign.size(), signature));

    return CopySpanToMutableSpan(ByteSpan{ signature.ConstBytes(), signature.Length() }, outSignBuffer);
}

CHIP_ERROR FactoryDataProvider::GetSetupDiscriminator(uint16_t & setupDiscriminator)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(setupDiscriminator) != mFlashDataHeader.SetupDiscriminator.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.SetupDiscriminator.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&setupDiscriminator,
                mFlashDataHeader.SetupDiscriminator.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SetSetupDiscriminator(uint16_t setupDiscriminator)
{
    //ReturnErrorOnFailure(BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_SetupDiscriminator, setupDiscriminator));
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pIterationCount(uint32_t & iterationCount)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(iterationCount) != mFlashDataHeader.Spake2pIterationCount.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pIterationCount.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&iterationCount,
                mFlashDataHeader.Spake2pIterationCount.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pSalt(MutableByteSpan & saltBuf)
{
    static constexpr size_t kSpake2pSalt_MaxBase64Len = BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length) + 1;

    char saltB64[kSpake2pSalt_MaxBase64Len] = { 0 };
    size_t saltB64Len                       = 0;

    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(kSpake2pSalt_MaxBase64Len < mFlashDataHeader.Spake2pSalt.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pSalt.off_set + mFlashDataHeader.header_length,
                (uint8_t *)saltB64,
                mFlashDataHeader.Spake2pSalt.length
                ),
            CHIP_ERROR_READ_FAILED);
    saltB64Len = mFlashDataHeader.Spake2pSalt.length;
    size_t saltLen = chip::Base64Decode32(saltB64, saltB64Len, reinterpret_cast<uint8_t *>(saltB64));
    ReturnErrorCodeIf(saltLen > saltBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(saltBuf.data(), saltB64, saltLen);
    saltBuf.reduce_size(saltLen);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pVerifier(MutableByteSpan & verifierBuf, size_t & verifierLen)
{
    static constexpr size_t kSpake2pSerializedVerifier_MaxBase64Len =
        BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_VerifierSerialized_Length) + 1;

    char verifierB64[kSpake2pSerializedVerifier_MaxBase64Len] = { 0 };
    size_t verifierB64Len                                     = 0;

    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(kSpake2pSerializedVerifier_MaxBase64Len < mFlashDataHeader.Spake2pVerifier.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pVerifier.off_set + mFlashDataHeader.header_length,
                (uint8_t *)verifierB64,
                mFlashDataHeader.Spake2pVerifier.length
                ),
            CHIP_ERROR_READ_FAILED);
    verifierB64Len = mFlashDataHeader.Spake2pVerifier.length;
    verifierLen = chip::Base64Decode32(verifierB64, verifierB64Len, reinterpret_cast<uint8_t *>(verifierB64));
    ReturnErrorCodeIf(verifierLen > verifierBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(verifierBuf.data(), verifierB64, verifierLen);
    verifierBuf.reduce_size(verifierLen);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSetupPasscode(uint32_t & setupPasscode)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(setupPasscode) != mFlashDataHeader.SetupPasscode.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.SetupPasscode.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&setupPasscode,
                mFlashDataHeader.SetupPasscode.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SetSetupPasscode(uint32_t setupPasscode)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR FactoryDataProvider::GetVendorName(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.VendorName.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.VendorName.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.VendorName.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetVendorId(uint16_t & vendorId)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(vendorId) != mFlashDataHeader.VendorId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.VendorId.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&vendorId,
                mFlashDataHeader.VendorId.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductName(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.ProductName.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.ProductName.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductName.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductId(uint16_t & productId)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(productId) != mFlashDataHeader.ProductId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.ProductId.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&productId,
                mFlashDataHeader.ProductId.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSerialNumber(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.SerialNumber.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.SerialNumber.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.SerialNumber.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
    uint32_t date;
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(date) < mFlashDataHeader.ManufacturingDate.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.ManufacturingDate.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&date,
                mFlashDataHeader.ManufacturingDate.length
                ),
            CHIP_ERROR_READ_FAILED);
    year = (date >> 16) & 0xFFFF;
    month = (date >> 8) & 0xFF;
    day = date & 0xFF;
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(sizeof(hardwareVersion) < mFlashDataHeader.HardwareVersion.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.HardwareVersion.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&hardwareVersion,
                mFlashDataHeader.HardwareVersion.length
                ),
            CHIP_ERROR_READ_FAILED);

    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.HardwareVersionString.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.HardwareVersionString.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.HardwareVersionString.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(uniqueIdSpan.size() < mFlashDataHeader.RotatingDeviceIdUniqueId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.RotatingDeviceIdUniqueId.off_set + mFlashDataHeader.header_length,
                uniqueIdSpan.data(),
                mFlashDataHeader.RotatingDeviceIdUniqueId.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetPartNumber(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.PartNumber.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.PartNumber.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.PartNumber.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
};
CHIP_ERROR FactoryDataProvider::GetProductURL(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.ProductURL.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.ProductURL.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductURL.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
};
CHIP_ERROR FactoryDataProvider::GetProductLabel(char * buf, size_t bufSize)
{
    ReturnErrorOnFailure(ReadFlashDataHeader());
    ReturnErrorCodeIf(bufSize < mFlashDataHeader.ProductLabel.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    ReturnErrorCodeIf(
            kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY_FLASH,
                flashDataHeaderOffset + mFlashDataHeader.ProductLabel.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductLabel.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
};

} // namespace DeviceLayer
} // namespace chip
