#include "../Client.inl"
#include "../Core/Channel.h"
#include "../Core/Snapshot.h"
#include "../../Modules/Administration/ChildSession.h"
#include "../../Modules/Execution/Client.h"
#include "../../Modules/File/Client.h"
#include "../../Modules/Rtc/Client.h"
#include "../../Modules/Recording/Client.h"
#include "../../Modules/Window/Capture.h"
#include "../../Network/Config.inl"
#include "Retry.inl"

#if defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) && \
    !defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_) && \
    !defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_) && \
    !defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_)
#error A Client build must enable at least one transport.
#endif

#include <Bcrypt.h>

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
#pragma comment(lib, "Ole32.lib")
#endif

VOID
NTAPI
ZpClient_QueryBuildCapabilities(
    _Out_ PZP_CLIENT_CAPABILITIES Capabilities)
{
    static const ZP_CLIENT_CAPABILITIES Build = {
        0
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_)
        | ZP_MODULE_BIT(1)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROCESS_)
        | ZP_MODULE_BIT(2)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SERVICE_)
        | ZP_MODULE_BIT(3)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
        | ZP_MODULE_BIT(4)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TERMINAL_)
        | ZP_MODULE_BIT(5)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EVENT_LOG_)
        | ZP_MODULE_BIT(6)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_REGISTRY_)
        | ZP_MODULE_BIT(7)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_)
        | ZP_MODULE_BIT(8)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_)
        | ZP_MODULE_BIT(9)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_)
        | ZP_MODULE_BIT(10)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TUNNEL_)
        | ZP_MODULE_BIT(11)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
        | ZP_MODULE_BIT(12)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WMI_)
        | ZP_MODULE_BIT(13)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_AUDIO_)
        | ZP_MODULE_BIT(14)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_VIDEO_)
        | ZP_MODULE_BIT(15)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RTC_)
        | ZP_MODULE_BIT(16)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SERIAL_)
        | ZP_MODULE_BIT(17)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
        | ZP_MODULE_BIT(18)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PORTABLE_DEVICE_)
        | ZP_MODULE_BIT(19)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
        | ZP_MODULE_BIT(20)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_)
        | ZP_MODULE_BIT(21)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_)
        | ZP_MODULE_BIT(22)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_)
        | ZP_MODULE_BIT(23)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_)
        | ZP_MODULE_BIT(24)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_)
        | ZP_MODULE_BIT(25)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_)
        | ZP_MODULE_BIT(26)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_)
        | ZP_MODULE_BIT(27)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_)
        | ZP_MODULE_BIT(28)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_)
        | ZP_MODULE_BIT(29)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_)
        | ZP_MODULE_BIT(30)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_)
        | ZP_MODULE_BIT(31)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
        | ZP_MODULE_BIT(32)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_)
        | ZP_MODULE_BIT(33)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_)
        | ZP_MODULE_BIT(34)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_)
        | ZP_MODULE_BIT(35)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_)
        | ZP_MODULE_BIT(36)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_)
        | ZP_MODULE_BIT(37)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_)
        | ZP_MODULE_BIT(38)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_)
        | ZP_MODULE_BIT(39)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_)
        | ZP_MODULE_BIT(40)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_)
        | ZP_MODULE_BIT(41)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_)
        | ZP_MODULE_BIT(42)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_)
        | ZP_MODULE_BIT(43)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_)
        | ZP_MODULE_BIT(44)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_)
        | ZP_MODULE_BIT(45)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)
        | ZP_MODULE_BIT(46)
#endif
        ,
        0
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_)
        | (1UL << ZpTransportQuic)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_)
        | (1UL << ZpTransportTcp)
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_)
        | (1UL << ZpTransportUdp)
#endif
        ,
        0
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_RUNTIME_DISCOVERY_))
        | ZP_CLIENT_EXECUTION_FEATURE_RUNTIME_DISCOVERY
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_SHELL_EXECUTE_))
        | ZP_CLIENT_EXECUTION_FEATURE_SHELL_EXECUTE
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_ALTERNATE_IDENTITY_))
        | ZP_CLIENT_EXECUTION_FEATURE_ALTERNATE_IDENTITY
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_APP_CONTAINER_))
        | ZP_CLIENT_EXECUTION_FEATURE_APP_CONTAINER
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_CUSTOM_TOKEN_))
        | ZP_CLIENT_EXECUTION_FEATURE_CUSTOM_TOKEN
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    (defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_) && \
     defined(_KNSOFT_ZPIGEON_BUILD_EXECUTION_STAGING_))
        | ZP_CLIENT_EXECUTION_FEATURE_STAGING
#endif
    };

    *Capabilities = Build;
}

static
NTSTATUS
ZpClient_ValidateConfig(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ PSIZE_T AllocationSize)
{
    NTSTATUS Status;
    SIZE_T Size, StringSize;
    ULONG Index;
    ZP_CLIENT_CAPABILITIES Capabilities;

    if (Config->EndpointCount == 0 ||
        Config->EndpointCount > ZP_ENDPOINT_MAX_COUNT ||
        Config->Endpoints == NULL ||
        Config->DeploymentRootCertificate == NULL ||
        Config->DeploymentRootCertificateLength == 0 ||
        Config->DeploymentRootCertificateLength > ZP_CERTIFICATE_MAX_SIZE ||
        Config->MaxRequestsPerConnection > ZP_CLIENT_MAX_REQUESTS_PER_CONNECTION ||
        Config->MaxRequestPayloadBytesPerConnection >
            ZP_CLIENT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION ||
        Config->MaxChannelsPerConnection >
            ZP_CLIENT_MAX_CHANNELS_PER_CONNECTION ||
        Config->ClientKeyScope > ZpClientKeyUser ||
        Config->StateCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Size = sizeof(ZP_CLIENT_OBJECT) +
           (SIZE_T)Config->EndpointCount * sizeof(ZP_ENDPOINT) +
           Config->DeploymentRootCertificateLength;
    Status = ZpConfig_AddStringSize(&Size, Config->ClientKeyName, FALSE, &StringSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (StringSize != 0 && Config->ClientKeyName[0] == UNICODE_NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = ZpConfig_AddStringSize(&Size, Config->Group, FALSE, &StringSize);
    if (!NT_SUCCESS(Status) || StringSize > (ZP_CLIENT_GROUP_MAX_LENGTH + 1) * sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Config->Group != NULL)
    {
        for (Index = 0; Config->Group[Index] != UNICODE_NULL; Index++)
        {
            if (Config->Group[Index] < 0x20 ||
                (Config->Group[Index] >= 0x7F && Config->Group[Index] <= 0x9F))
            {
                return STATUS_INVALID_PARAMETER;
            }
        }
    }

    ZpClient_QueryBuildCapabilities(&Capabilities);
    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        PCZP_ENDPOINT Endpoint = &Config->Endpoints[Index];

        if (!ZpConfig_IsTransportValid(Endpoint->Transport) || Endpoint->Port == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (!FlagOn(Capabilities.Transports, 1UL << Endpoint->Transport))
        {
            return STATUS_NOT_SUPPORTED;
        }
        if (Endpoint->RequireTls13 && Endpoint->Transport != ZpTransportTcp)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpConfig_AddStringSize(&Size, Endpoint->Host, TRUE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Status = ZpConfig_AddStringSize(&Size, Endpoint->ServerName, TRUE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    *AllocationSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Create(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ ZP_CLIENT_HANDLE* Client)
{
    NTSTATUS Status;
    SIZE_T AllocationSize;
    PBYTE Cursor;
    PZP_CLIENT_OBJECT Object;
    PZP_ENDPOINT Endpoints;
    ULONG Index;

    Status = ZpClient_ValidateConfig(Config, &AllocationSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Object = Mem_Alloc(AllocationSize);
    if (Object == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Object, sizeof(*Object));
    RtlInitializeSRWLock(&Object->Lock);
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    RtlInitializeSRWLock(&Object->FileEnumerationLock);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    RtlInitializeSRWLock(&Object->ArchiveEnumerationLock);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_)
    RtlInitializeSRWLock(&Object->ExecutionLock);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    RtlInitializeSRWLock(&Object->RecordingLock);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROCESS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
    RtlInitializeSRWLock(&Object->SnapshotLock);
#endif
    InitializeListHead(&Object->InboundRequests);
    InitializeListHead(&Object->LocalChannels);
    for (Index = 0; Index < ZP_CLIENT_LOOKUP_BUCKET_COUNT; Index++)
    {
        InitializeListHead(&Object->InboundRequestBuckets[Index]);
        InitializeListHead(&Object->LocalChannelBuckets[Index]);
    }
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_)
    InitializeListHead(&Object->ExecutionJobs);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    InitializeListHead(&Object->RecordingJobs);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    InitializeListHead(&Object->FileEnumerations);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    InitializeListHead(&Object->ArchiveEnumerations);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROCESS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
    InitializeListHead(&Object->Snapshots);
#endif
    Object->NextLocalChannelId = 1;
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    Object->NextFileEnumerationId = 1;
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    Object->NextArchiveEnumerationId = 1;
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_)
    Object->NextExecutionJobId = 1;
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    if (FAILED(CoIncrementMTAUsage(&Object->CaptureMtaCookie)))
    {
        Mem_Free(Object);
        return STATUS_DLL_INIT_FAILED;
    }
    ZpWindowCapture_Initialize();
#endif
    Object->State = ZpClientStateStopped;
    Object->Config = *Config;
    Object->Config.ConnectTimeoutMilliseconds = Config->ConnectTimeoutMilliseconds != 0 ?
                                                    Config->ConnectTimeoutMilliseconds :
                                                    ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS;
    Object->Config.MaxRequestsPerConnection = Config->MaxRequestsPerConnection != 0 ?
                                                  Config->MaxRequestsPerConnection :
                                                  ZP_CLIENT_DEFAULT_MAX_REQUESTS_PER_CONNECTION;
    Object->Config.MaxRequestPayloadBytesPerConnection =
        Config->MaxRequestPayloadBytesPerConnection != 0 ?
            Config->MaxRequestPayloadBytesPerConnection :
            ZP_CLIENT_DEFAULT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION;
    Object->Config.MaxChannelsPerConnection =
        Config->MaxChannelsPerConnection != 0 ?
            Config->MaxChannelsPerConnection :
            ZP_CLIENT_DEFAULT_MAX_CHANNELS_PER_CONNECTION;
    Cursor = Add2Ptr(Object, sizeof(*Object));
    Endpoints = (PZP_ENDPOINT)Cursor;
    Cursor += (SIZE_T)Config->EndpointCount * sizeof(*Endpoints);
    Object->Config.Endpoints = Endpoints;
    ZpConfig_CopyString(&Cursor, Config->ClientKeyName, &Object->Config.ClientKeyName);
    ZpConfig_CopyString(&Cursor, Config->Group, &Object->Config.Group);

    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        Endpoints[Index] = Config->Endpoints[Index];
        ZpConfig_CopyString(&Cursor, Config->Endpoints[Index].Host, &Endpoints[Index].Host);
        ZpConfig_CopyString(&Cursor, Config->Endpoints[Index].ServerName, &Endpoints[Index].ServerName);
    }
    Object->Config.DeploymentRootCertificate = Cursor;
    RtlCopyMemory(Cursor,
                  Config->DeploymentRootCertificate,
                  Config->DeploymentRootCertificateLength);
    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        ZP_TRANSPORT_TYPE Transport = Config->Endpoints[Index].Transport;

        if (Object->TransportOperations[Transport] != NULL)
        {
            continue;
        }
        switch (Transport)
        {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_)
            case ZpTransportQuic:
                ZpClientQuic_Configure(Object);
                break;
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_)
            case ZpTransportTcp:
                ZpClientTcp_Configure(Object);
                break;
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_)
            case ZpTransportUdp:
                ZpClientUdp_Configure(Object);
                break;
#endif

            default:
                break;
        }
    }
    *Client = (ZP_CLIENT_HANDLE)Object;
    return STATUS_SUCCESS;
}

static
_Success_(return != FALSE)
LOGICAL
ZpClient_FindNextEndpoint(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG StartIndex,
    _Out_ PULONG EndpointIndex)
{
    ULONG Index;

    for (Index = StartIndex; Index < Object->Config.EndpointCount; Index++)
    {
        if (Object->TransportOperations[Object->Config.Endpoints[Index].Transport] != NULL)
        {
            *EndpointIndex = Index;
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
ZpClient_ScheduleRetry(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL RestartRound)
{
    LARGE_INTEGER DueTime;
    ULONG Delay, EndpointIndex;
    ULONG RandomValue;

    if (RestartRound)
    {
        Object->NextEndpointIndex = 0;
    }
    if (ZpClient_FindNextEndpoint(Object,
                                  Object->NextEndpointIndex,
                                  &EndpointIndex))
    {
        Object->NextEndpointIndex = EndpointIndex;
        Delay = 1;
    }
    else
    {
        Object->NextEndpointIndex = 0;
        if (!NT_SUCCESS(BCryptGenRandom(NULL,
                                        (PUCHAR)&RandomValue,
                                        sizeof(RandomValue),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            RandomValue = ZpClientRetry_GetBaseDelay(Object->FailureRound) *
                          ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT / 100;
        }
        Delay = ZpClientRetry_GetDelay(Object->FailureRound, RandomValue);
        if (Object->FailureRound != MAXULONG)
        {
            Object->FailureRound++;
        }
    }
    if (!NT_SUCCESS(ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                         ZpClientStateRetryWait,
                                         Status)))
    {
        return;
    }

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State == ZpClientStateRetryWait)
    {
        Object->RetryPending = TRUE;
        Object->RetryDelay = Delay;
        if (!Object->StartPending)
        {
            DueTime.QuadPart = -(LONGLONG)Delay * 10000;
            SetThreadpoolTimer(Object->RetryTimer,
                               (PFILETIME)&DueTime,
                               0,
                               0);
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
}

static
ZP_STATUS
ZpClient_StartEndpoints(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    ULONG Index;

    for (Index = Object->NextEndpointIndex;
         Index < Object->Config.EndpointCount;
         Index++)
    {
        Operations = Object->TransportOperations[Object->Config.Endpoints[Index].Transport];
        if (Operations == NULL)
        {
            continue;
        }
        TransportContext = Object->TransportContexts[Object->Config.Endpoints[Index].Transport];
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Object->ActiveTransport = Object->Config.Endpoints[Index].Transport;
        Object->EndpointIndex = Index;
        Object->NextEndpointIndex = Index + 1;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = Operations->Start(TransportContext, Index);
        if (ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
    }
    return Status;
}

static
VOID
ZpClient_CompleteStart(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status)
{
    LARGE_INTEGER DueTime;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    ZP_CLIENT_STATE State;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->StartPending = FALSE;
    State = Object->State;
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    if (State == ZpClientStateRetryWait && Object->RetryPending)
    {
        DueTime.QuadPart = -(LONGLONG)Object->RetryDelay * 10000;
        SetThreadpoolTimer(Object->RetryTimer,
                           (PFILETIME)&DueTime,
                           0,
                           0);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    if (State == ZpClientStateStopping)
    {
        if (ZpStatus_IsSuccess(Status))
        {
            Operations->Stop(TransportContext);
        }
        else
        {
            ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                 ZpClientStateStopped,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS));
        }
    }
    else if (!ZpStatus_IsSuccess(Status) &&
             State == ZpClientStateConnecting)
    {
        ZpClient_ScheduleRetry(Object, Status, FALSE);
    }
}

static
VOID
CALLBACK
ZpClient_RetryTimerCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context,
    _Inout_ PTP_TIMER Timer)
{
    PZP_CLIENT_OBJECT Object = Context;
    NTSTATUS Status;
    ZP_STATUS StartStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    LOGICAL Start;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
    if (Object == NULL)
    {
        return;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Start = Object->State == ZpClientStateRetryWait &&
            Object->RetryPending &&
            !Object->StartPending;
    if (Start)
    {
        Object->RetryPending = FALSE;
        Object->StartPending = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Start)
    {
        return;
    }

    Status = ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                  ZpClientStateConnecting,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockShared(&Object->Lock);
        Start = Object->State == ZpClientStateConnecting;
        RtlReleaseSRWLockShared(&Object->Lock);
        if (Start)
        {
            StartStatus = ZpClient_StartEndpoints(Object);
        }
    }
    else
    {
        StartStatus = ZpStatus_FromNtStatus(Status);
    }
    ZpClient_CompleteStart(Object, StartStatus);
}

NTSTATUS
NTAPI
ZpClient_Start(
    _In_ ZP_CLIENT_HANDLE Client)
{
    ZP_STATUS Status;
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    ULONG EndpointIndex;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (!ZpClient_FindNextEndpoint(Object, 0, &EndpointIndex))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_NOT_SUPPORTED;
    }
    if (Object->RetryTimer == NULL)
    {
        Object->RetryTimer = CreateThreadpoolTimer(ZpClient_RetryTimerCallback,
                                                    Object,
                                                    NULL);
        if (Object->RetryTimer == NULL)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            return STATUS_NO_MEMORY;
        }
    }
    Object->State = ZpClientStateConnecting;
    Object->CallbackCount++;
    Object->ActiveTransport = Object->Config.Endpoints[EndpointIndex].Transport;
    Object->EndpointIndex = EndpointIndex;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateConnecting,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS),
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    if (Object->State != ZpClientStateConnecting)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    Object->NextEndpointIndex = 0;
    Object->FailureRound = 0;
    Object->ReadyTickCount = 0;
    Object->StartPending = TRUE;
    Object->RetryPending = FALSE;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Status = ZpClient_StartEndpoints(Object);
    ZpClient_CompleteStart(Object, Status);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Stop(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    LOGICAL CompleteStop;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State == ZpClientStateStopped || Object->State == ZpClientStateStopping)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    CompleteStop = Object->State == ZpClientStateRetryWait && !Object->StartPending;
    Object->State = ZpClientStateStopping;
    Object->CallbackCount++;
    Object->RetryPending = FALSE;
    if (Object->RetryTimer != NULL)
    {
        SetThreadpoolTimer(Object->RetryTimer, NULL, 0, 0);
    }
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateStopping,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS),
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (CompleteStop)
    {
        ZpClient_NotifyState(Client, ZpClientStateStopped, ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
    else
    {
        Operations->Stop(TransportContext);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    if (!ZpConfig_IsTransportValid(Transport) ||
        Operations == NULL ||
        Operations->Start == NULL ||
        Operations->Stop == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->TransportOperations[Transport] = Operations;
    Object->TransportContexts[Transport] = Context;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

static
LOGICAL
ZpClient_IsTransitionValid(
    _In_ ZP_CLIENT_STATE CurrentState,
    _In_ ZP_CLIENT_STATE State)
{
    switch (CurrentState)
    {
        case ZpClientStateConnecting:
            return State == ZpClientStateAuthenticating ||
                   State == ZpClientStateRetryWait ||
                   State == ZpClientStateStopped;

        case ZpClientStateAuthenticating:
            return State == ZpClientStateReady ||
                   State == ZpClientStateRetryWait ||
                   State == ZpClientStateStopped;

        case ZpClientStateReady:
            return State == ZpClientStateRetryWait || State == ZpClientStateStopped;

        case ZpClientStateRetryWait:
            return State == ZpClientStateConnecting || State == ZpClientStateStopped;

        case ZpClientStateStopping:
            return State == ZpClientStateStopped;
    }
    return FALSE;
}

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpClient_IsTransitionValid(Object->State, State))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->State = State;
    if (State == ZpClientStateAuthenticating)
    {
        Object->HighestInboundRequestId = 0;
    }
    else if (State == ZpClientStateReady)
    {
        Object->ReadyTickCount = GetTickCount64();
        Object->NextLocalChannelId = 1;
    }
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Object->Config.StateCallback(Client, State, Status, Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    ZP_CLIENT_STATE State;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    State = Object->State;
    if (State == ZpClientStateReady)
    {
        if (GetTickCount64() - Object->ReadyTickCount >=
            ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS)
        {
            Object->FailureRound = 0;
        }
        Object->NextEndpointIndex = 0;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    ZpClient_CloseInboundRequests(Client);
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RTC_)
    ZpRtc_Close(Object);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FILE_)
    ZpFile_ResetEnumeration(Object);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROCESS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
    ZpClientSnapshot_CloseAll(Object);
#endif
    ZpClientLocalChannel_CloseAll(Object, Status);
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    ZpRecording_StopAll(Object);
#endif
    if (State == ZpClientStateStopping)
    {
        ZpClient_NotifyState(Client, ZpClientStateStopped, Status);
        return;
    }
    else if (State == ZpClientStateConnecting ||
             State == ZpClientStateAuthenticating ||
             State == ZpClientStateReady)
    {
        ZpClient_ScheduleRetry(Object,
                               Status,
                               State == ZpClientStateReady);
    }
}

NTSTATUS
NTAPI
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped ||
        Object->CallbackCount != 0 ||
        !IsListEmpty(&Object->LocalChannels))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_DEVICE_BUSY;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Object->RetryTimer != NULL)
    {
        SetThreadpoolTimer(Object->RetryTimer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(Object->RetryTimer, TRUE);
        CloseThreadpoolTimer(Object->RetryTimer);
        Object->RetryTimer = NULL;
    }
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_)
    if (Object->TransportOperations[ZpTransportQuic] != NULL)
    {
        ZpClientQuic_Uninitialize(&Object->QuicTransport);
    }
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_)
    if (Object->TransportOperations[ZpTransportTcp] != NULL)
    {
        ZpClientTcp_Uninitialize(&Object->TcpTransport);
    }
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_)
    if (Object->TransportOperations[ZpTransportUdp] != NULL)
    {
        ZpClientUdp_Uninitialize(&Object->UdpTransport);
    }
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_)
    ZpExecution_Cleanup(Object);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    ZpRecording_Cleanup(Object);
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_)
    ZpRdpChildSession_Close();
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    ZpWindowCapture_Uninitialize();
    CoDecrementMTAUsage(Object->CaptureMtaCookie);
#endif
    Mem_Free(Object);
    return STATUS_SUCCESS;
}
