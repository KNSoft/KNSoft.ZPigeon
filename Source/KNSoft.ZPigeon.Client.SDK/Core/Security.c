#include "Security.h"

#include <sddl.h>

#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")

NTSTATUS
ZpSecurity_QueryDacl(
    _In_ HANDLE Object,
    _Outptr_ PUNICODE_STRING* Sddl)
{
    PSECURITY_DESCRIPTOR Descriptor;
    PUNICODE_STRING Value;
    PWSTR String;
    ULONG Length, Required;
    NTSTATUS Status;

    Status = NtQuerySecurityObject(Object,
                                   OWNER_SECURITY_INFORMATION |
                                       GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION,
                                   NULL,
                                   0,
                                   &Required);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        return Status;
    }
    Descriptor = Mem_Alloc(Required);
    if (Descriptor == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NtQuerySecurityObject(Object,
                                   OWNER_SECURITY_INFORMATION |
                                       GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION,
                                   Descriptor,
                                   Required,
                                   &Required);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Descriptor);
        return Status;
    }
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            Descriptor,
            SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION |
                GROUP_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION,
            &String,
            NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    Mem_Free(Descriptor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Length = (ULONG)wcslen(String);
    Value = Length <= (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR) ?
                NT_AllocStringW((USHORT)Length) : NULL;
    if (Value != NULL)
    {
        RtlCopyMemory(Value->Buffer, String, (Length + 1) * sizeof(WCHAR));
    }
    LocalFree(String);
    if (Value == NULL)
    {
        return Length > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR) ?
                   STATUS_NAME_TOO_LONG : STATUS_NO_MEMORY;
    }
    *Sddl = Value;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpSecurity_SetDacl(
    _In_ HANDLE Object,
    _In_ PCWSTR Sddl)
{
    PSECURITY_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            Sddl,
            SDDL_REVISION_1,
            &Descriptor,
            NULL))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Status = NtSetSecurityObject(Object,
                                 DACL_SECURITY_INFORMATION,
                                 Descriptor);
    LocalFree(Descriptor);
    return Status;
}
