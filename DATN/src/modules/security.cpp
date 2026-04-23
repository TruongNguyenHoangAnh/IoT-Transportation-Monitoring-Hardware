#include "security.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

// ===== AES-128 CBC ENCRYPTION CONFIG =====
static const uint8_t aes_key[16] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
};
static const uint8_t aes_iv[16] = {
    0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x07,0x18,
    0x29,0x3A,0x4B,0x5C,0x6D,0x7E,0x8F,0x90
};

#define HMAC_SECRET "datn_252_secret_key"

String encryptDataToAESBase64(const String& jsonStr) {
    mbedtls_aes_context aes;
    uint8_t iv[16];
    memcpy(iv, aes_iv, sizeof(iv));

    size_t input_len = jsonStr.length();
    size_t pad = 16 - (input_len % 16);
    size_t enc_len = input_len + pad;

    uint8_t buf[enc_len];
    memcpy(buf, jsonStr.c_str(), input_len);
    memset(buf + input_len, pad, pad);

    uint8_t output[enc_len];

    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, aes_key, 128);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, enc_len, iv, buf, output);
    mbedtls_aes_free(&aes);

    unsigned char base64_buf[512];
    size_t base64_len = 0;
    mbedtls_base64_encode(base64_buf, sizeof(base64_buf), &base64_len, output, enc_len);

    return String((char*)base64_buf, base64_len);
}

String hmacSha256(const String &message) {
    unsigned char output[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET));
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length());
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);

    static const char hexDigits[] = "0123456789abcdef";
    char hex_output[65];
    for (int i = 0; i < 32; i++) {
        unsigned char value = output[i];
        hex_output[i * 2]     = hexDigits[(value >> 4) & 0x0F];
        hex_output[i * 2 + 1] = hexDigits[value & 0x0F];
    }
    hex_output[64] = '\0';

    return String(hex_output);
}