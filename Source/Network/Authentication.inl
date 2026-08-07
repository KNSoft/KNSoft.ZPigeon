#pragma once

#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

static const BYTE ZpAuthenticationLabel[] = "KNSoft.ZPigeon.ClientAuth.v1";

static
NTSTATUS
ZpAuthentication_Hash(
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _In_reads_bytes_(ZP_CLIENT_PUBLIC_KEY_SIZE) const BYTE* PublicKey,
    _Out_writes_bytes_(32) BYTE* Hash)
{
    BCRYPT_ALG_HANDLE Algorithm;
    NTSTATUS Status;
    BYTE Input[(sizeof(ZpAuthenticationLabel) - 1) + 1 +
               ZP_SERVER_CHALLENGE_SIZE + ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0 };
    BYTE* Cursor = Input;

    RtlCopyMemory(Cursor,
                  ZpAuthenticationLabel,
                  sizeof(ZpAuthenticationLabel) - 1);
    Cursor += sizeof(ZpAuthenticationLabel) - 1;
    *Cursor++ = 0;
    RtlCopyMemory(Cursor, Challenge, ZP_SERVER_CHALLENGE_SIZE);
    Cursor += ZP_SERVER_CHALLENGE_SIZE;
    RtlCopyMemory(Cursor, PublicKey, ZP_CLIENT_PUBLIC_KEY_SIZE);

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = BCryptHash(Algorithm,
                        NULL,
                        0,
                        Input,
                        sizeof(Input),
                        Hash,
                        32);
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Status;
}

static
NTSTATUS
ZpAuthentication_Verify(
    _In_reads_bytes_(ZP_CLIENT_PUBLIC_KEY_SIZE) const BYTE* PublicKey,
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _In_reads_bytes_(ZP_CLIENT_SIGNATURE_SIZE) const BYTE* Signature)
{
    BCRYPT_ALG_HANDLE Algorithm;
    BCRYPT_KEY_HANDLE Key;
    BCRYPT_ECCKEY_BLOB* Blob;
    BYTE BlobBuffer[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BYTE Hash[32];
    NTSTATUS Status;

    if (PublicKey[0] != 0x04)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Blob = (BCRYPT_ECCKEY_BLOB*)BlobBuffer;
    Blob->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    Blob->cbKey = 32;
    RtlCopyMemory(BlobBuffer + sizeof(*Blob), PublicKey + 1, 64);
    Status = ZpAuthentication_Hash(Challenge, PublicKey, Hash);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_ECDSA_P256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        RtlSecureZeroMemory(Hash, sizeof(Hash));
        return Status;
    }
    Status = BCryptImportKeyPair(Algorithm,
                                 NULL,
                                 BCRYPT_ECCPUBLIC_BLOB,
                                 &Key,
                                 BlobBuffer,
                                 sizeof(BlobBuffer),
                                 0);
    if (NT_SUCCESS(Status))
    {
        Status = BCryptVerifySignature(Key,
                                       NULL,
                                       Hash,
                                       sizeof(Hash),
                                       (PBYTE)Signature,
                                       ZP_CLIENT_SIGNATURE_SIZE,
                                       0);
        BCryptDestroyKey(Key);
    }
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    RtlSecureZeroMemory(Hash, sizeof(Hash));
    return Status;
}

static
NTSTATUS
ZpAuthentication_GetClientId(
    _In_reads_bytes_(ZP_CLIENT_PUBLIC_KEY_SIZE) const BYTE* PublicKey,
    _Out_writes_bytes_(32) BYTE* ClientId)
{
    BCRYPT_ALG_HANDLE Algorithm;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = BCryptHash(Algorithm,
                        NULL,
                        0,
                        (PBYTE)PublicKey,
                        ZP_CLIENT_PUBLIC_KEY_SIZE,
                        ClientId,
                        32);
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Status;
}
