#pragma once
#include <crypto/CHIPCryptoPAL.h>
namespace chip { namespace DeviceLayer {
CHIP_ERROR IdentitySign(const ByteSpan &, MutableByteSpan &);
CHIP_ERROR IdentityCertificate(unsigned field, MutableByteSpan &);
}}
extern "C" void matter_identity(char *, int, int, char **);
