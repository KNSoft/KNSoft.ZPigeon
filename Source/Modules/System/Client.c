#include "Client.h"

#include <KNSoft/MakeLifeEasier/System/Info.h>

NTSTATUS
ZpSystem_ExecuteInfo(
    _Out_writes_bytes_to_(BufferSize, *PayloadLength) PBYTE Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG PayloadLength)
{
    SYSTEM_BASIC_INFORMATION BasicInfo;
    ZP_SYSTEM_INFO Info;
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD ComputerNameLength = ARRAYSIZE(ComputerName);
    ULONG ReturnLength;
    NTSTATUS Status;

    switch (Sys_GetMachineType())
    {
        case IMAGE_FILE_MACHINE_I386:
            Info.Architecture = ZpSystemArchitectureX86;
            break;

        case IMAGE_FILE_MACHINE_AMD64:
            Info.Architecture = ZpSystemArchitectureX64;
            break;

        case IMAGE_FILE_MACHINE_ARM64:
            Info.Architecture = ZpSystemArchitectureArm64;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }
    Status = NtQuerySystemInformation(SystemBasicInformation,
                                      &BasicInfo,
                                      sizeof(BasicInfo),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (!GetComputerNameW(ComputerName, &ComputerNameLength))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Info.MajorVersion = SharedUserData->NtMajorVersion;
    Info.MinorVersion = SharedUserData->NtMinorVersion;
    Info.BuildNumber = SharedUserData->NtBuildNumber;
    Info.ProcessorCount = SharedUserData->ActiveProcessorCount;
    Info.PhysicalMemoryBytes = (ULONGLONG)BasicInfo.NumberOfPhysicalPages * BasicInfo.PageSize;
    Info.ComputerName = ComputerName;
    Info.ComputerNameLength = ComputerNameLength;
    return ZpSystem_EncodeInfo(&Info, Buffer, BufferSize, PayloadLength);
}
