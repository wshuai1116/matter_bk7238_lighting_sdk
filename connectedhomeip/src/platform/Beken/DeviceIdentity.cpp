#include "DeviceIdentity.h"
#include "easyflash.h"
#include <platform/CHIPDeviceLayer.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <lib/support/CodeUtils.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/sha256.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace chip { namespace DeviceLayer {
namespace {
constexpr char KeyName[]="id.key.v1", DoneName[]="id.done.v1";
struct KeyRecord { uint32_t magic; uint16_t vid,pid,size; uint8_t der[160]; };
struct Package {uint32_t magic; uint16_t cd,dac,pai; uint8_t bytes[2000];};
static Package upload;
static size_t received;
static KeyRecord key;
static Package stored;
static void wipe(void *p,size_t n){volatile uint8_t *v=(volatile uint8_t*)p;while(n--)*v++=0;}
static int rng(void *,unsigned char *out,size_t n){return Crypto::DRBG_get_bytes(out,n)==CHIP_NO_ERROR?0:-1;}
static bool exists(const char *name){struct env_node_obj n;return ef_get_env_obj(name,&n);}
static bool readExact(const char *name,void *p,size_t n){size_t size=0;return ef_get_env_blob(name,p,n,&size)==n&&size==n;}
static bool loadKey(mbedtls_pk_context *pk){
 if(!readExact(KeyName,&key,sizeof(key))||key.magic!=0x3144494b||key.size==0||key.size>sizeof(key.der))return false;
 return mbedtls_pk_parse_key(pk,key.der,key.size,nullptr,0)==0;
}
static bool loadDone(){return readExact(DoneName,&stored,sizeof(stored))&&stored.magic==0x31444943&&stored.cd&&stored.dac&&stored.pai&&size_t(stored.cd)+stored.dac+stored.pai<=sizeof(stored.bytes);}
static bool saveVerified(const char *name,const void *p,size_t n){
 if(ef_set_env_blob(name,p,n)!=EF_NO_ERR)return false;
 uint8_t *check=(uint8_t*)malloc(n);if(!check)return false;
 bool ok=readExact(name,check,n)&&!memcmp(check,p,n);wipe(check,n);free(check);return ok;
}
static void printHex(const char *tag,const uint8_t *p,size_t n){printf("\r\nIDP:%s:",tag);for(size_t i=0;i<n;i++)printf("%02X",p[i]);printf("\r\n");}
static bool validate(mbedtls_pk_context *pk){
 mbedtls_x509_crt dac,pai;mbedtls_x509_crt_init(&dac);mbedtls_x509_crt_init(&pai);
 bool ok=false;uint8_t digest[32],a[65],b[65];size_t an=0,bn=0;
 const uint8_t *d=upload.bytes+upload.cd,*p=d+upload.dac;
 if(mbedtls_x509_crt_parse_der(&dac,d,upload.dac)||mbedtls_x509_crt_parse_der(&pai,p,upload.pai))goto end;
 if(!mbedtls_pk_can_do(&dac.pk,MBEDTLS_PK_ECKEY)||!mbedtls_pk_can_do(pk,MBEDTLS_PK_ECKEY))goto end;
 if(mbedtls_ecp_point_write_binary(&mbedtls_pk_ec(*pk)->grp,&mbedtls_pk_ec(*pk)->Q,MBEDTLS_ECP_PF_UNCOMPRESSED,&an,a,sizeof(a))||
 mbedtls_ecp_point_write_binary(&mbedtls_pk_ec(dac.pk)->grp,&mbedtls_pk_ec(dac.pk)->Q,MBEDTLS_ECP_PF_UNCOMPRESSED,&bn,b,sizeof(b)))goto end;
 if(an!=bn||memcmp(a,b,an)||dac.issuer_raw.len!=pai.subject_raw.len||memcmp(dac.issuer_raw.p,pai.subject_raw.p,dac.issuer_raw.len))goto end;
 if(dac.sig_md!=MBEDTLS_MD_SHA256||mbedtls_sha256_ret(dac.tbs.p,dac.tbs.len,digest,0))goto end;
 ok=mbedtls_pk_verify(&pai.pk,MBEDTLS_MD_SHA256,digest,sizeof(digest),dac.sig.p,dac.sig.len)==0;
end:mbedtls_x509_crt_free(&dac);mbedtls_x509_crt_free(&pai);return ok;
}
static int nibble(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static CHIP_ERROR command(int argc,char **argv){
 if(argc<2)return CHIP_ERROR_INVALID_ARGUMENT;
 if(!strcmp(argv[1],"status")){printf("\r\nIDP:STATUS:%s\r\n",exists(DoneName)?(loadDone()?"SEALED":"CORRUPT"):"OPEN");return CHIP_NO_ERROR;}
 if(!strcmp(argv[1],"get")){if(!loadDone())return CHIP_ERROR_INCORRECT_STATE;printHex("CERTS",(uint8_t*)&stored,10+stored.cd+stored.dac+stored.pai);return CHIP_NO_ERROR;}
 if(!strcmp(argv[1],"proof")&&argc==3){
  if(strlen(argv[2])!=64)return CHIP_ERROR_INVALID_ARGUMENT;
  uint8_t challenge[32],signature[64];
  for(size_t i=0;i<32;i++){int a=nibble(argv[2][i*2]),b=nibble(argv[2][i*2+1]);if(a<0||b<0)return CHIP_ERROR_INVALID_ARGUMENT;challenge[i]=(a<<4)|b;}
  MutableByteSpan span(signature);ReturnErrorOnFailure(IdentitySign(ByteSpan(challenge),span));
  printHex("PROOF",signature,span.size());return CHIP_NO_ERROR;
 }
 if(exists(DoneName))return CHIP_ERROR_INCORRECT_STATE;
 mbedtls_pk_context pk;mbedtls_pk_init(&pk);CHIP_ERROR err=CHIP_ERROR_INTERNAL;
 if(!strcmp(argv[1],"csr")&&argc==4){
  char *e1,*e2;unsigned long vid=strtoul(argv[2],&e1,16),pid=strtoul(argv[3],&e2,16);
  if(*e1||*e2||vid>65535||pid>65535){err=CHIP_ERROR_INVALID_ARGUMENT;goto exit;}
  if(exists(KeyName)){
   if(!loadKey(&pk)||key.vid!=vid||key.pid!=pid){err=CHIP_ERROR_INCORRECT_STATE;goto exit;}
  }else{
   memset(&key,0,sizeof(key));key.magic=0x3144494b;key.vid=vid;key.pid=pid;
   if(mbedtls_pk_setup(&pk,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))||mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(pk),rng,nullptr))goto exit;
   int n=mbedtls_pk_write_key_der(&pk,key.der,sizeof(key.der));if(n<=0)goto exit;
   memmove(key.der,key.der+sizeof(key.der)-n,n);key.size=n;
   if(!saveVerified(KeyName,&key,sizeof(key)))goto exit;
  }
  {
   mbedtls_x509write_csr csr;mbedtls_x509write_csr_init(&csr);
   mbedtls_x509write_csr_set_key(&csr,&pk);mbedtls_x509write_csr_set_md_alg(&csr,MBEDTLS_MD_SHA256);
   char v[5],p[5];snprintf(v,sizeof(v),"%04X",unsigned(vid));snprintf(p,sizeof(p),"%04X",unsigned(pid));
   int ret=mbedtls_x509write_csr_set_subject_name(&csr,"CN=BK7238 Device DAC");
   const char vo[]="\x2b\x06\x01\x04\x01\x82\xa2\x7c\x02\x01",po[]="\x2b\x06\x01\x04\x01\x82\xa2\x7c\x02\x02";
   if(!ret){
    auto *vn=mbedtls_asn1_store_named_data(&csr.subject,vo,10,(const unsigned char*)v,4);
    auto *pn=mbedtls_asn1_store_named_data(&csr.subject,po,10,(const unsigned char*)p,4);
    if(!vn||!pn)ret=-1;else{vn->val.tag=MBEDTLS_ASN1_UTF8_STRING;pn->val.tag=MBEDTLS_ASN1_UTF8_STRING;}
   }
   uint8_t out[512];if(!ret)ret=mbedtls_x509write_csr_der(&csr,out,sizeof(out),rng,nullptr);
   if(ret>0){printHex("CSR",out+sizeof(out)-ret,ret);err=CHIP_NO_ERROR;}
   mbedtls_x509write_csr_free(&csr);
  }
 }else if(!strcmp(argv[1],"put")&&argc==4){
  char *end;size_t offset=strtoul(argv[2],&end,10),n=strlen(argv[3]);
  if(*end||n%2||n>128||offset>sizeof(upload)||n/2>sizeof(upload)-offset){err=CHIP_ERROR_INVALID_ARGUMENT;goto exit;}
  if(offset==0){memset(&upload,0,sizeof(upload));received=0;}
  if(offset!=received){err=CHIP_ERROR_INVALID_ARGUMENT;goto exit;}
  for(size_t i=0;i<n;i+=2){int a=nibble(argv[3][i]),b=nibble(argv[3][i+1]);if(a<0||b<0){err=CHIP_ERROR_INVALID_ARGUMENT;goto exit;}((uint8_t*)&upload)[offset+i/2]=(a<<4)|b;}
  received+=n/2;printf("\r\nIDP:OFFSET:%u\r\n",unsigned(received));err=CHIP_NO_ERROR;
 }else if(!strcmp(argv[1],"commit")&&argc==2){
  size_t total=size_t(upload.cd)+upload.dac+upload.pai;
  if(upload.magic!=0x31444943||!upload.cd||!upload.dac||!upload.pai||total>sizeof(upload.bytes)||received!=10+total||!loadKey(&pk)||!validate(&pk)){err=CHIP_ERROR_INVALID_ARGUMENT;goto exit;}
  if(!saveVerified(DoneName,&upload,sizeof(upload)))goto exit;
  printf("\r\nIDP:SEALED\r\n");err=CHIP_NO_ERROR;
 }
exit:mbedtls_pk_free(&pk);wipe(&key,sizeof(key));return err;
}
}
CHIP_ERROR IdentityCertificate(unsigned field,MutableByteSpan &out){
 if(!loadDone())return CHIP_ERROR_INCORRECT_STATE;
 size_t n=field==0?stored.cd:field==1?stored.dac:stored.pai;
 size_t offset=field==0?0:field==1?stored.cd:stored.cd+stored.dac;
 return CopySpanToMutableSpan(ByteSpan(stored.bytes+offset,n),out);
}
CHIP_ERROR IdentitySign(const ByteSpan &message,MutableByteSpan &out){
 if(!loadDone())return CHIP_ERROR_INCORRECT_STATE;
 mbedtls_pk_context pk;mbedtls_pk_init(&pk);CHIP_ERROR err=CHIP_ERROR_INTERNAL;
 uint8_t hash[32],der[80];size_t len=0;
 if(loadKey(&pk)&&!mbedtls_sha256_ret(message.data(),message.size(),hash,0)&&!mbedtls_pk_sign(&pk,MBEDTLS_MD_SHA256,hash,sizeof(hash),der,&len,rng,nullptr))err=Crypto::EcdsaAsn1SignatureToRaw(32,ByteSpan(der,len),out);
 mbedtls_pk_free(&pk);wipe(&key,sizeof(key));return err;
}
}}
extern "C" void matter_identity(char *,int,int argc,char **argv){
 chip::DeviceLayer::PlatformMgr().LockChipStack();
 CHIP_ERROR err=chip::DeviceLayer::command(argc,argv);
 printf("\r\nIDP:RESULT:%lu\r\n",(unsigned long)err.AsInteger());
 chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}
