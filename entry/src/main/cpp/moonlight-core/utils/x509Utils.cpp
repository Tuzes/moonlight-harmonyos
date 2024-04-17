//
// Created on 2023/12/8.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "x509Utils.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>


void THROW_BAD_ALLOC_IF_NULL(void *target) {
    if (target == nullptr) {
        ERR_print_errors_fp(stderr);
        abort();
    }
}

long getFileSize(FILE *file) {
    long size;

    // 记录当前文件位置
    long currentPosition = ftell(file);

    // 将文件位置设置到文件末尾
    fseek(file, 0, SEEK_END);

    // 获取文件位置，即文件大小
    size = ftell(file);

    // 恢复文件位置
    fseek(file, currentPosition, SEEK_SET);

    return size;
}

EVP_PKEY *generateKey() {

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    THROW_BAD_ALLOC_IF_NULL(ctx);

    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);

    // pk must be initialized on input
    EVP_PKEY *pk = NULL;
    EVP_PKEY_keygen(ctx, &pk);

    EVP_PKEY_CTX_free(ctx);
    THROW_BAD_ALLOC_IF_NULL(pk);
    return pk;
}

int generate_x509_certificate(char *cert_path, char *key_path) {
    EVP_PKEY *pk = nullptr;
    X509 *cert = nullptr;
    FILE *cert_file = nullptr;
    FILE *key_file = nullptr;

    // 初始化 OpenSSL 库
    OpenSSL_add_all_algorithms();
    // 创建 X.509 证书
    cert = X509_new();
    // 创建 RSA 密钥对
    pk = generateKey();

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * 365 * 20); // 20 yrs
#else
    ASN1_TIME *before = ASN1_STRING_dup(X509_get0_notBefore(cert));
    THROW_BAD_ALLOC_IF_NULL(before);
    ASN1_TIME *after = ASN1_STRING_dup(X509_get0_notAfter(cert));
    THROW_BAD_ALLOC_IF_NULL(after);

    X509_gmtime_adj(before, 0);
    X509_gmtime_adj(after, 60 * 60 * 24 * 365 * 20); // 20 yrs

    X509_set1_notBefore(cert, before);
    X509_set1_notAfter(cert, after);

    ASN1_STRING_free(before);
    ASN1_STRING_free(after);
#endif

    X509_set_pubkey(cert, pk);

    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<unsigned char *>(const_cast<char *>("NVIDIA GameStream Client")),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name);

    X509_sign(cert, pk, EVP_sha256());

    // 将证书和密钥写入文件
    cert_file = fopen(cert_path, "w");
    int ret = PEM_write_X509(cert_file, cert);
    fclose(cert_file);
    key_file = fopen(key_path, "w");
    ret = PEM_write_PrivateKey(key_file, pk, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(key_file);
    FILE *key_cer_file = nullptr;
    key_cer_file = fopen(strcat(key_path, ".cer"), "w");
    ret = i2d_PrivateKey_fp(key_cer_file, pk);
    fclose(key_cer_file);

    // 释放资源
    EVP_PKEY_free(pk);
    X509_free(cert);

    // 清理 OpenSSL 库
    EVP_cleanup();

    return 0;
}
char *get_value_string(napi_env env, napi_value value) {
    size_t length;
    napi_get_value_string_utf8(env, value, nullptr, 0, &length);
    char *buffer = (char *)malloc(length + 1);
    napi_get_value_string_utf8(env, value, buffer, length + 1, &length);
    return buffer;
}
napi_value generate_certificate(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char *certPath = get_value_string(env, args[0]);
    char *keyPath = get_value_string(env, args[1]);
    generate_x509_certificate(certPath, keyPath);
    return 0;
}
napi_value verifySignature(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void *data;
    void *signature;
    void *serverCertificate;
    size_t dataLength;
    size_t signatureLength;
    size_t serverCertificateLength;
    napi_get_typedarray_info(
        env,
        args[0],
        nullptr,
        &dataLength,
        &data,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    napi_get_typedarray_info(
        env,
        args[1],
        nullptr,
        &signatureLength,
        &signature,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    napi_get_typedarray_info(
        env,
        args[2],
        nullptr,
        &serverCertificateLength,
        &serverCertificate,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    BIO *bio = BIO_new_mem_buf(serverCertificate, serverCertificateLength);
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);

    EVP_PKEY *pubKey = X509_get_pubkey(cert);
    THROW_BAD_ALLOC_IF_NULL(pubKey);

    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
    THROW_BAD_ALLOC_IF_NULL(mdctx);

    EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubKey);
    EVP_DigestVerifyUpdate(mdctx, data, dataLength);
    int result = EVP_DigestVerifyFinal(mdctx, reinterpret_cast<unsigned char *>(signature), signatureLength);

    EVP_PKEY_free(pubKey);
    EVP_MD_CTX_destroy(mdctx);
    X509_free(cert);
    napi_value ret;
    napi_get_boolean(env, result > 0, &ret);
    return ret;
}

napi_value createTypedArray(napi_env env, size_t length, napi_typedarray_type type, void *data) {
    napi_value arrayBuffer;
    void *arrayBufferPtr;
    napi_value uint8Array;
    // 创建一个 ArrayBuffer
    napi_status status = napi_create_arraybuffer(env, length, &arrayBufferPtr, &arrayBuffer);
    if (status != napi_ok) {
        // 处理错误
        return NULL;
    }
    memcpy(arrayBufferPtr, data, length);

    // 创建一个 TypedArray，使用指定的类型和 ArrayBuffer
    status = napi_create_typedarray(env, type, length, arrayBuffer, 0, &uint8Array);
    if (status != napi_ok) {
        // 处理错误
        return NULL;
    }

    return uint8Array;
}

napi_value signMessage(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void *message;
    size_t messageLength;
    void *privateKey;
    size_t privateKeyLength;
    napi_get_typedarray_info(
        env,
        args[0],
        nullptr,
        &messageLength,
        &message,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    napi_get_typedarray_info(
        env,
        args[1],
        nullptr,
        &privateKeyLength,
        &privateKey,
        nullptr,
        nullptr);
    EVP_MD_CTX *ctx = EVP_MD_CTX_create();
    THROW_BAD_ALLOC_IF_NULL(ctx);

    BIO *bio = BIO_new_mem_buf(privateKey, privateKeyLength);
    THROW_BAD_ALLOC_IF_NULL(bio);
    // const unsigned char *ptr = (const unsigned char *)privateKey;
    // EVP_PKEY *m_PrivateKey = d2i_PrivateKey(EVP_PKEY_RSA, NULL, &ptr, privateKeyLength);

    EVP_PKEY *m_PrivateKey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);

    EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, m_PrivateKey);
    EVP_DigestSignUpdate(ctx, reinterpret_cast<unsigned char *>(message), messageLength);

    size_t signatureLength = 0;
    EVP_DigestSignFinal(ctx, NULL, &signatureLength);

    unsigned char *signature = (unsigned char *)malloc(signatureLength);

    EVP_DigestSignFinal(ctx, signature, &signatureLength);
    napi_value uint8Array =
        createTypedArray(env, signatureLength, napi_uint8_array, signature);

    EVP_MD_CTX_destroy(ctx);

    return uint8Array;
}

napi_value getSignatureFromPemCert(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void *serverCertificate;
    size_t serverCertificateLength;
    napi_get_typedarray_info(
        env,
        args[0],
        nullptr,
        &serverCertificateLength,
        &serverCertificate,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    BIO *bio = BIO_new_mem_buf(serverCertificate, serverCertificateLength);
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);

#if (OPENSSL_VERSION_NUMBER < 0x10002000L)
    ASN1_BIT_STRING *asnSignature = cert->signature;
#elif (OPENSSL_VERSION_NUMBER < 0x10100000L)
    ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#else
    const ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#endif
    X509_free(cert);
    return createTypedArray(env, asnSignature->length, napi_uint8_array, asnSignature->data);
}

napi_value encrypt(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void *message;
    size_t messageLength;
    void *privateKey;
    size_t privateKeyLength;
    napi_get_typedarray_info(
        env,
        args[0],
        nullptr,
        &messageLength,
        &message,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    napi_get_typedarray_info(
        env,
        args[1],
        nullptr,
        &privateKeyLength,
        &privateKey,
        nullptr,
        nullptr);

    void *ciphertext = malloc(messageLength);
    EVP_CIPHER_CTX *cipher;
    int ciphertextLen;

    cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    EVP_EncryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char *>(privateKey), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    EVP_EncryptUpdate(cipher,
                      reinterpret_cast<unsigned char *>(ciphertext),
                      &ciphertextLen,
                      reinterpret_cast<const unsigned char *>(message),
                      messageLength);
    EVP_CIPHER_CTX_free(cipher);

    return createTypedArray(env, messageLength, napi_uint8_array, ciphertext);
}
napi_value decrypt(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void *message;
    size_t messageLength;
    void *privateKey;
    size_t privateKeyLength;
    napi_get_typedarray_info(
        env,
        args[0],
        nullptr,
        &messageLength,
        &message,
        nullptr, // 可选的 ArrayBuffer
        nullptr  // 可选的偏移
    );
    napi_get_typedarray_info(
        env,
        args[1],
        nullptr,
        &privateKeyLength,
        &privateKey,
        nullptr,
        nullptr);
    void *plaintext = malloc(messageLength);
    EVP_CIPHER_CTX *cipher;
    int plaintextLen;

    cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    EVP_DecryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char *>(privateKey), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    EVP_DecryptUpdate(cipher,
                      reinterpret_cast<unsigned char *>(plaintext),
                      &plaintextLen,
                      reinterpret_cast<const unsigned char *>(message),
                      messageLength);

    EVP_CIPHER_CTX_free(cipher);

    return createTypedArray(env, messageLength, napi_uint8_array, plaintext);
}