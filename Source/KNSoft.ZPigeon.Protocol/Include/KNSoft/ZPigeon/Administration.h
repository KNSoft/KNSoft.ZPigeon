#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_ADMINISTRATION_MODULE_ID 9
#define ZP_ADMINISTRATION_MODULE_VERSION 1

#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_USERS 1
#define ZP_ADMINISTRATION_OPERATION_CONTROL_USER 2
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_SOFTWARE 3
#define ZP_ADMINISTRATION_OPERATION_CONTROL_SOFTWARE 4
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_HARDWARE 5
#define ZP_ADMINISTRATION_OPERATION_CONTROL_HARDWARE 6
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES 7
#define ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE 8
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_TASKS 9
#define ZP_ADMINISTRATION_OPERATION_CONTROL_TASK 10
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIREWALL 11
#define ZP_ADMINISTRATION_OPERATION_CONTROL_FIREWALL 12
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_POWER 13
#define ZP_ADMINISTRATION_OPERATION_CONTROL_POWER 14
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES 15
#define ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE 16
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM 17
#define ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM 18
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_WLAN 19
#define ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN 20
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATES 21
#define ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE 22
#define ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE 23
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIPBOARD 24
#define ZP_ADMINISTRATION_OPERATION_CONTROL_CLIPBOARD 25
#define ZP_ADMINISTRATION_OPERATION_WAIT_CLIPBOARD 26
#define ZP_ADMINISTRATION_OPERATION_QUERY_WLAN_PROFILE 27
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_CREDENTIALS 28
#define ZP_ADMINISTRATION_OPERATION_QUERY_CREDENTIAL 29
#define ZP_ADMINISTRATION_OPERATION_CONTROL_CREDENTIAL 30
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIRMWARE_VARIABLES 31
#define ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE 32
#define ZP_ADMINISTRATION_OPERATION_CONTROL_FIRMWARE 33
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_PUBLISHED_SHARES 34
#define ZP_ADMINISTRATION_OPERATION_QUERY_PUBLISHED_SHARE 35
#define ZP_ADMINISTRATION_OPERATION_CONTROL_PUBLISHED_SHARE 36
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_CONNECTIONS 37
#define ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_CONNECTION 38
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ADAPTERS 39
#define ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ADAPTER 40
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ROUTES 41
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ENDPOINTS 42
#define ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ROUTE 43
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_SESSIONS 44
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_LOGON_SESSIONS 45
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_PAGE_FILES 46
#define ZP_ADMINISTRATION_OPERATION_CONTROL_PAGE_FILE 47
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_BLUETOOTH 48
#define ZP_ADMINISTRATION_OPERATION_CONTROL_BLUETOOTH 49
#define ZP_ADMINISTRATION_OPERATION_WAIT_KEYBOARD 50
#define ZP_ADMINISTRATION_OPERATION_QUERY_LOCATION 51
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_FONTS 52
#define ZP_ADMINISTRATION_OPERATION_CONTROL_FONT 53
#define ZP_ADMINISTRATION_OPERATION_ENUMERATE_APP_CONTAINERS 54
#define ZP_ADMINISTRATION_OPERATION_CONTROL_APP_CONTAINER 55
#define ZP_ADMINISTRATION_OPERATION_QUERY_OBJECT_DIRECTORY 56

typedef USHORT ZP_ADMINISTRATION_KIND, *PZP_ADMINISTRATION_KIND;

#define ZpAdministrationKindUser ((ZP_ADMINISTRATION_KIND)1)
#define ZpAdministrationKindDesktopProgram ((ZP_ADMINISTRATION_KIND)2)
#define ZpAdministrationKindWindowsApp ((ZP_ADMINISTRATION_KIND)3)
#define ZpAdministrationKindWindowsFeature ((ZP_ADMINISTRATION_KIND)4)
#define ZpAdministrationKindDevice ((ZP_ADMINISTRATION_KIND)5)
#define ZpAdministrationKindUpdate ((ZP_ADMINISTRATION_KIND)6)
#define ZpAdministrationKindTask ((ZP_ADMINISTRATION_KIND)7)
#define ZpAdministrationKindUpdateHistory ((ZP_ADMINISTRATION_KIND)8)
#define ZpAdministrationKindTaskFolder ((ZP_ADMINISTRATION_KIND)9)
#define ZpAdministrationKindFirewallProfile ((ZP_ADMINISTRATION_KIND)10)
#define ZpAdministrationKindFirewallRule ((ZP_ADMINISTRATION_KIND)11)
#define ZpAdministrationKindPowerSetting ((ZP_ADMINISTRATION_KIND)12)
#define ZpAdministrationKindPowerPlan ((ZP_ADMINISTRATION_KIND)13)
#define ZpAdministrationKindUps ((ZP_ADMINISTRATION_KIND)14)
#define ZpAdministrationKindSystemInformation ((ZP_ADMINISTRATION_KIND)15)
#define ZpAdministrationKindWlanInterface ((ZP_ADMINISTRATION_KIND)16)
#define ZpAdministrationKindWlanNetwork ((ZP_ADMINISTRATION_KIND)17)
#define ZpAdministrationKindWlanProfile ((ZP_ADMINISTRATION_KIND)18)
#define ZpAdministrationKindEnvironmentVariable ((ZP_ADMINISTRATION_KIND)19)
#define ZpAdministrationKindCertificateStore ((ZP_ADMINISTRATION_KIND)20)
#define ZpAdministrationKindCertificate ((ZP_ADMINISTRATION_KIND)21)
#define ZpAdministrationKindCertificateDetails ((ZP_ADMINISTRATION_KIND)22)
#define ZpAdministrationKindCertificateChain ((ZP_ADMINISTRATION_KIND)23)
#define ZpAdministrationKindClipboardFormat ((ZP_ADMINISTRATION_KIND)24)
#define ZpAdministrationKindClipboardState ((ZP_ADMINISTRATION_KIND)25)
#define ZpAdministrationKindWindowsCredential ((ZP_ADMINISTRATION_KIND)26)
#define ZpAdministrationKindWebCredential ((ZP_ADMINISTRATION_KIND)27)
#define ZpAdministrationKindFirmwareVariable ((ZP_ADMINISTRATION_KIND)28)
#define ZpAdministrationKindFirmwareBootEntry ((ZP_ADMINISTRATION_KIND)29)
#define ZpAdministrationKindCpuidSnapshot ((ZP_ADMINISTRATION_KIND)30)
#define ZpAdministrationKindSmbiosTable ((ZP_ADMINISTRATION_KIND)31)
#define ZpAdministrationKindAcpiTable ((ZP_ADMINISTRATION_KIND)32)
#define ZpAdministrationKindPublishedShare ((ZP_ADMINISTRATION_KIND)33)
#define ZpAdministrationKindNetworkConnection ((ZP_ADMINISTRATION_KIND)34)
#define ZpAdministrationKindNetworkAdapter ((ZP_ADMINISTRATION_KIND)35)
#define ZpAdministrationKindNetworkAdapterAddress ((ZP_ADMINISTRATION_KIND)36)
#define ZpAdministrationKindNetworkRoute ((ZP_ADMINISTRATION_KIND)37)
#define ZpAdministrationKindTcpEndpoint ((ZP_ADMINISTRATION_KIND)38)
#define ZpAdministrationKindUdpEndpoint ((ZP_ADMINISTRATION_KIND)39)
#define ZpAdministrationKindSession ((ZP_ADMINISTRATION_KIND)40)
#define ZpAdministrationKindLogonSession ((ZP_ADMINISTRATION_KIND)41)
#define ZpAdministrationKindPageFile ((ZP_ADMINISTRATION_KIND)42)
#define ZpAdministrationKindBluetoothRadio ((ZP_ADMINISTRATION_KIND)43)
#define ZpAdministrationKindBluetoothDevice ((ZP_ADMINISTRATION_KIND)44)
#define ZpAdministrationKindKeyboardEvent ((ZP_ADMINISTRATION_KIND)45)
#define ZpAdministrationKindLocation ((ZP_ADMINISTRATION_KIND)46)
#define ZpAdministrationKindFont ((ZP_ADMINISTRATION_KIND)47)

#define ZpAdministrationKindAppContainerProfile ((ZP_ADMINISTRATION_KIND)48)
#define ZpAdministrationKindAppContainerCapability ((ZP_ADMINISTRATION_KIND)49)
#define ZpAdministrationKindAppContainerBinary ((ZP_ADMINISTRATION_KIND)50)
#define ZpAdministrationKindObjectDirectory ((ZP_ADMINISTRATION_KIND)51)
#define ZpAdministrationKindObject ((ZP_ADMINISTRATION_KIND)52)

#define ZP_ADMINISTRATION_APP_CONTAINER_FLAG_LOOPBACK 0x00000001
#define ZP_ADMINISTRATION_APP_CONTAINER_FLAG_PACKAGED 0x00000002

#define ZP_ADMINISTRATION_FLAG_PARTIAL 0x80000000

#define ZP_ADMINISTRATION_CREDENTIAL_FLAG_SECRET_AVAILABLE 0x00000001
#define ZP_ADMINISTRATION_CREDENTIAL_FLAG_PROMPT 0x00000002
#define ZP_ADMINISTRATION_CREDENTIAL_PERSIST_SHIFT 8
#define ZP_ADMINISTRATION_CREDENTIAL_PERSIST_MASK 0x00000300

typedef USHORT ZP_ADMINISTRATION_ACTION, *PZP_ADMINISTRATION_ACTION;

#define ZpAdministrationActionCreate ((ZP_ADMINISTRATION_ACTION)1)
#define ZpAdministrationActionDelete ((ZP_ADMINISTRATION_ACTION)2)
#define ZpAdministrationActionEnable ((ZP_ADMINISTRATION_ACTION)3)
#define ZpAdministrationActionDisable ((ZP_ADMINISTRATION_ACTION)4)
#define ZpAdministrationActionSetPassword ((ZP_ADMINISTRATION_ACTION)5)
#define ZpAdministrationActionRun ((ZP_ADMINISTRATION_ACTION)6)
#define ZpAdministrationActionStop ((ZP_ADMINISTRATION_ACTION)7)
#define ZpAdministrationActionInstall ((ZP_ADMINISTRATION_ACTION)8)
#define ZpAdministrationActionUninstall ((ZP_ADMINISTRATION_ACTION)9)
#define ZpAdministrationActionRefresh ((ZP_ADMINISTRATION_ACTION)10)
#define ZpAdministrationActionRename ((ZP_ADMINISTRATION_ACTION)11)
#define ZpAdministrationActionRestart ((ZP_ADMINISTRATION_ACTION)12)
#define ZpAdministrationActionCheck ((ZP_ADMINISTRATION_ACTION)13)
#define ZpAdministrationActionAllow ((ZP_ADMINISTRATION_ACTION)14)
#define ZpAdministrationActionBlock ((ZP_ADMINISTRATION_ACTION)15)
#define ZpAdministrationActionSleep ((ZP_ADMINISTRATION_ACTION)16)
#define ZpAdministrationActionHibernate ((ZP_ADMINISTRATION_ACTION)17)
#define ZpAdministrationActionShutdown ((ZP_ADMINISTRATION_ACTION)18)
#define ZpAdministrationActionSignOut ((ZP_ADMINISTRATION_ACTION)19)
#define ZpAdministrationActionLock ((ZP_ADMINISTRATION_ACTION)20)
#define ZpAdministrationActionActivate ((ZP_ADMINISTRATION_ACTION)21)
#define ZpAdministrationActionFirmware ((ZP_ADMINISTRATION_ACTION)22)
#define ZpAdministrationActionConfigure ((ZP_ADMINISTRATION_ACTION)23)
#define ZpAdministrationActionConnect ((ZP_ADMINISTRATION_ACTION)24)
#define ZpAdministrationActionDisconnect ((ZP_ADMINISTRATION_ACTION)25)
#define ZpAdministrationActionSetPermissions ((ZP_ADMINISTRATION_ACTION)26)
#define ZpAdministrationActionTurnOffDisplay ((ZP_ADMINISTRATION_ACTION)27)

typedef struct _ZP_ADMINISTRATION_RECORD
{
    ZP_ADMINISTRATION_KIND Kind;
    ULONG State;
    ULONG Flags;
    ULONGLONG Value;
    PCWCH Identity;
    ULONG IdentityLength;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH Detail;
    ULONG DetailLength;
} ZP_ADMINISTRATION_RECORD, *PZP_ADMINISTRATION_RECORD;

typedef const ZP_ADMINISTRATION_RECORD* PCZP_ADMINISTRATION_RECORD;

typedef struct _ZP_ADMINISTRATION_RECORD_VIEW
{
    ZP_ADMINISTRATION_KIND Kind;
    ULONG State;
    ULONG Flags;
    ULONGLONG Value;
    ZP_STRING_VIEW Identity;
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW Description;
    ZP_STRING_VIEW Detail;
} ZP_ADMINISTRATION_RECORD_VIEW, *PZP_ADMINISTRATION_RECORD_VIEW;

typedef struct _ZP_ADMINISTRATION_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_ADMINISTRATION_LIST_VIEW, *PZP_ADMINISTRATION_LIST_VIEW;

typedef const ZP_ADMINISTRATION_LIST_VIEW* PCZP_ADMINISTRATION_LIST_VIEW;

typedef struct _ZP_ADMINISTRATION_CONTROL_VIEW
{
    ZP_ADMINISTRATION_ACTION Action;
    ZP_STRING_VIEW Identity;
    ZP_STRING_VIEW Argument;
    ZP_STRING_VIEW Secret;
} ZP_ADMINISTRATION_CONTROL_VIEW, *PZP_ADMINISTRATION_CONTROL_VIEW;

typedef const ZP_ADMINISTRATION_CONTROL_VIEW* PCZP_ADMINISTRATION_CONTROL_VIEW;

NTSTATUS
ZpAdministration_EncodeList(
    _In_reads_opt_(RecordCount) PCZP_ADMINISTRATION_RECORD Records,
    _In_ ULONG RecordCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAdministration_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_ADMINISTRATION_LIST_VIEW View);

NTSTATUS
ZpAdministration_GetRecord(
    _In_ PCZP_ADMINISTRATION_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_ADMINISTRATION_RECORD_VIEW Record);

NTSTATUS
ZpAdministration_EncodeControl(
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAdministration_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_ADMINISTRATION_CONTROL_VIEW Control);

NTSTATUS
ZpAdministration_EncodeQuery(
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAdministration_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Identity);

EXTERN_C_END
