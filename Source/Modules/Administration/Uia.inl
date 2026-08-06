#include <UIAutomationClient.h>

#pragma comment(lib, "Uiautomationcore.lib")

#define ZP_UI_AUTOMATION_MAX_CHILDREN 4096
#define ZP_UIA_PROPERTY_UNAVAILABLE 0x00000001
#define ZP_UIA_PROPERTY_NOT_SUPPORTED 0x00000002

static
IUIAutomationElement*
ZpUia_ResolveElement(
    _In_ IUIAutomation* Automation,
    _In_ IUIAutomationTreeWalker* Walker,
    _In_ PCWSTR Path)
{
    IUIAutomationElement* Element = NULL;
    IUIAutomationElement* Next;
    PCWSTR Cursor;
    PWSTR End;
    ULONG Index, Position;
    HRESULT Result;

    if (wcsncmp(Path, L"root", RTL_NUMBER_OF(L"root") - 1) != 0 ||
        (Path[4] != UNICODE_NULL && Path[4] != L'/'))
    {
        return NULL;
    }
    Result = IUIAutomation_GetRootElement(Automation, &Element);
    if (FAILED(Result)) return NULL;
    Cursor = Path + 4;
    while (*Cursor != UNICODE_NULL)
    {
        Cursor++;
        Index = wcstoul(Cursor, &End, 10);
        if (End == Cursor || (*End != UNICODE_NULL && *End != L'/'))
        {
            IUIAutomationElement_Release(Element);
            return NULL;
        }
        Result = IUIAutomationTreeWalker_GetFirstChildElement(Walker, Element, &Next);
        IUIAutomationElement_Release(Element);
        Element = SUCCEEDED(Result) ? Next : NULL;
        for (Position = 0; Element != NULL && Position < Index; Position++)
        {
            Result = IUIAutomationTreeWalker_GetNextSiblingElement(Walker, Element, &Next);
            IUIAutomationElement_Release(Element);
            Element = SUCCEEDED(Result) ? Next : NULL;
        }
        if (Element == NULL) return NULL;
        Cursor = End;
    }
    return Element;
}

static
NTSTATUS
ZpUia_AddElement(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ IUIAutomationTreeWalker* Walker,
    _In_ IUIAutomationElement* Element,
    _In_ PCWSTR ParentPath,
    _In_ ULONG Index)
{
    IUIAutomationElement* Child = NULL;
    BSTR Name = NULL, Type = NULL, AutomationId = NULL, ClassName = NULL, Framework = NULL;
    WCHAR Identity[512];
    PWSTR Detail;
    SIZE_T DetailLength;
    CONTROLTYPEID ControlType = 0;
    int ProcessId = 0;
    ULONG State = 0;
    HRESULT Result;
    NTSTATUS Status;

    if (_snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, L"%s/%lu", ParentPath, Index) < 0)
        return STATUS_NAME_TOO_LONG;
    IUIAutomationElement_get_CurrentProcessId(Element, &ProcessId);
    IUIAutomationElement_get_CurrentControlType(Element, &ControlType);
    IUIAutomationElement_get_CurrentName(Element, &Name);
    IUIAutomationElement_get_CurrentLocalizedControlType(Element, &Type);
    IUIAutomationElement_get_CurrentAutomationId(Element, &AutomationId);
    IUIAutomationElement_get_CurrentClassName(Element, &ClassName);
    IUIAutomationElement_get_CurrentFrameworkId(Element, &Framework);
    Result = IUIAutomationTreeWalker_GetFirstChildElement(Walker, Element, &Child);
    if (SUCCEEDED(Result) && Child != NULL)
    {
        State = 1;
        IUIAutomationElement_Release(Child);
    }
    DetailLength = (AutomationId == NULL ? 0 : SysStringLen(AutomationId)) +
                   (ClassName == NULL ? 0 : SysStringLen(ClassName)) +
                   (Framework == NULL ? 0 : SysStringLen(Framework)) + 3;
    Detail = Mem_Alloc(DetailLength * sizeof(WCHAR));
    if (Detail == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    else
    {
        _snwprintf_s(Detail,
                     DetailLength,
                     _TRUNCATE,
                     L"%s\n%s\n%s",
                     AutomationId == NULL ? L"" : AutomationId,
                     ClassName == NULL ? L"" : ClassName,
                     Framework == NULL ? L"" : Framework);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindUiAutomationElement,
                                             State,
                                             ControlType,
                                             (ULONG)ProcessId,
                                             Identity,
                                             Name,
                                             Type,
                                             Detail);
        Mem_Free(Detail);
    }
    SysFreeString(Framework);
    SysFreeString(ClassName);
    SysFreeString(AutomationId);
    SysFreeString(Type);
    SysFreeString(Name);
    return Status;
}

static
ZP_STATUS
ZpAdministration_QueryUiAutomationChildren(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    IUIAutomation* Automation = NULL;
    IUIAutomationTreeWalker* Walker = NULL;
    IUIAutomationElement* Parent = NULL;
    IUIAutomationElement* Element = NULL;
    IUIAutomationElement* Next;
    PWSTR Path;
    ULONG Index = 0;
    HRESULT Result, InitializeResult;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Identity->Length < 4 || Identity->Length >= 512)
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Path = ZpAdministration_CopyView(Identity);
    if (Path == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        Mem_Free(Path);
        return ZpStatus_FromNtStatus(NTSTATUS_FROM_WIN32(HRESULT_CODE(InitializeResult)));
    }
    Result = CoCreateInstance(&CLSID_CUIAutomation,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IUIAutomation,
                              (PVOID*)&Automation);
    if (SUCCEEDED(Result)) Result = IUIAutomation_get_ControlViewWalker(Automation, &Walker);
    if (SUCCEEDED(Result)) Parent = ZpUia_ResolveElement(Automation, Walker, Path);
    if (Parent == NULL && SUCCEEDED(Result)) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (SUCCEEDED(Result)) Result = IUIAutomationTreeWalker_GetFirstChildElement(Walker, Parent, &Element);
    while (SUCCEEDED(Result) && Element != NULL && Index < ZP_UI_AUTOMATION_MAX_CHILDREN)
    {
        Status = ZpUia_AddElement(&Builder, Walker, Element, Path, Index++);
        if (!NT_SUCCESS(Status)) break;
        Result = IUIAutomationTreeWalker_GetNextSiblingElement(Walker, Element, &Next);
        IUIAutomationElement_Release(Element);
        Element = SUCCEEDED(Result) ? Next : NULL;
    }
    if (Element != NULL) IUIAutomationElement_Release(Element);
    if (Parent != NULL) IUIAutomationElement_Release(Parent);
    if (Walker != NULL) IUIAutomationTreeWalker_Release(Walker);
    if (Automation != NULL) IUIAutomation_Release(Automation);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    Mem_Free(Path);
    if (SUCCEEDED(Result) && NT_SUCCESS(Status))
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) : ZpStatus_FromNtStatus(Status);
}

typedef struct _ZP_UI_AUTOMATION_PROPERTY
{
    const PROPERTYID* Id;
    PCWSTR Name;
    PCWSTR Group;
} ZP_UI_AUTOMATION_PROPERTY, *PZP_UI_AUTOMATION_PROPERTY;

typedef const ZP_UI_AUTOMATION_PROPERTY* PCZP_UI_AUTOMATION_PROPERTY;

static const ZP_UI_AUTOMATION_PROPERTY ZpUiaProperties[] = {
    { &UIA_NamePropertyId, L"name", L"properties" },
    { &UIA_ControlTypePropertyId, L"controlType", L"properties" },
    { &UIA_LocalizedControlTypePropertyId, L"localizedControlType", L"properties" },
    { &UIA_AutomationIdPropertyId, L"automationId", L"properties" },
    { &UIA_ClassNamePropertyId, L"className", L"properties" },
    { &UIA_FrameworkIdPropertyId, L"frameworkId", L"properties" },
    { &UIA_ProcessIdPropertyId, L"processId", L"properties" },
    { &UIA_RuntimeIdPropertyId, L"runtimeId", L"properties" },
    { &UIA_NativeWindowHandlePropertyId, L"nativeWindowHandle", L"properties" },
    { &UIA_BoundingRectanglePropertyId, L"boundingRectangle", L"properties" },
    { &UIA_ClickablePointPropertyId, L"clickablePoint", L"properties" },
    { &UIA_IsEnabledPropertyId, L"isEnabled", L"properties" },
    { &UIA_IsOffscreenPropertyId, L"isOffscreen", L"properties" },
    { &UIA_IsKeyboardFocusablePropertyId, L"isKeyboardFocusable", L"properties" },
    { &UIA_HasKeyboardFocusPropertyId, L"hasKeyboardFocus", L"properties" },
    { &UIA_AccessKeyPropertyId, L"accessKey", L"properties" },
    { &UIA_AcceleratorKeyPropertyId, L"acceleratorKey", L"properties" },
    { &UIA_HelpTextPropertyId, L"helpText", L"properties" },
    { &UIA_IsPasswordPropertyId, L"isPassword", L"properties" },
    { &UIA_IsRequiredForFormPropertyId, L"isRequiredForForm", L"properties" },
    { &UIA_IsDialogPropertyId, L"isDialog", L"properties" },
    { &UIA_IsControlElementPropertyId, L"isControlElement", L"properties" },
    { &UIA_IsContentElementPropertyId, L"isContentElement", L"properties" },
    { &UIA_ItemStatusPropertyId, L"itemStatus", L"properties" },
    { &UIA_ItemTypePropertyId, L"itemType", L"properties" },
    { &UIA_OrientationPropertyId, L"orientation", L"properties" },
    { &UIA_CulturePropertyId, L"culture", L"properties" },
    { &UIA_PositionInSetPropertyId, L"positionInSet", L"properties" },
    { &UIA_SizeOfSetPropertyId, L"sizeOfSet", L"properties" },
    { &UIA_LevelPropertyId, L"level", L"properties" },
    { &UIA_FullDescriptionPropertyId, L"fullDescription", L"properties" },
    { &UIA_AriaRolePropertyId, L"ariaRole", L"properties" },
    { &UIA_AriaPropertiesPropertyId, L"ariaProperties", L"properties" },
    { &UIA_ProviderDescriptionPropertyId, L"providerDescription", L"properties" },
    { &UIA_LiveSettingPropertyId, L"liveSetting", L"properties" },
    { &UIA_IsInvokePatternAvailablePropertyId, L"invoke", L"patterns" },
    { &UIA_IsSelectionPatternAvailablePropertyId, L"selection", L"patterns" },
    { &UIA_IsValuePatternAvailablePropertyId, L"value", L"patterns" },
    { &UIA_IsRangeValuePatternAvailablePropertyId, L"rangeValue", L"patterns" },
    { &UIA_IsScrollPatternAvailablePropertyId, L"scroll", L"patterns" },
    { &UIA_IsExpandCollapsePatternAvailablePropertyId, L"expandCollapse", L"patterns" },
    { &UIA_IsGridPatternAvailablePropertyId, L"grid", L"patterns" },
    { &UIA_IsGridItemPatternAvailablePropertyId, L"gridItem", L"patterns" },
    { &UIA_IsMultipleViewPatternAvailablePropertyId, L"multipleView", L"patterns" },
    { &UIA_IsWindowPatternAvailablePropertyId, L"window", L"patterns" },
    { &UIA_IsSelectionItemPatternAvailablePropertyId, L"selectionItem", L"patterns" },
    { &UIA_IsDockPatternAvailablePropertyId, L"dock", L"patterns" },
    { &UIA_IsTablePatternAvailablePropertyId, L"table", L"patterns" },
    { &UIA_IsTableItemPatternAvailablePropertyId, L"tableItem", L"patterns" },
    { &UIA_IsTextPatternAvailablePropertyId, L"text", L"patterns" },
    { &UIA_IsTogglePatternAvailablePropertyId, L"toggle", L"patterns" },
    { &UIA_IsTransformPatternAvailablePropertyId, L"transform", L"patterns" },
    { &UIA_IsScrollItemPatternAvailablePropertyId, L"scrollItem", L"patterns" },
    { &UIA_IsAnnotationPatternAvailablePropertyId, L"annotation", L"patterns" },
    { &UIA_IsDragPatternAvailablePropertyId, L"drag", L"patterns" },
    { &UIA_IsDropTargetPatternAvailablePropertyId, L"dropTarget", L"patterns" },
    { &UIA_IsTextEditPatternAvailablePropertyId, L"textEdit", L"patterns" },
    { &UIA_IsCustomNavigationPatternAvailablePropertyId, L"customNavigation", L"patterns" },
    { &UIA_IsItemContainerPatternAvailablePropertyId, L"itemContainer", L"patterns" },
    { &UIA_IsVirtualizedItemPatternAvailablePropertyId, L"virtualizedItem", L"patterns" },
    { &UIA_IsSynchronizedInputPatternAvailablePropertyId, L"synchronizedInput", L"patterns" },
    { &UIA_IsObjectModelPatternAvailablePropertyId, L"objectModel", L"patterns" },
    { &UIA_IsSpreadsheetPatternAvailablePropertyId, L"spreadsheet", L"patterns" },
    { &UIA_IsSpreadsheetItemPatternAvailablePropertyId, L"spreadsheetItem", L"patterns" },
    { &UIA_IsStylesPatternAvailablePropertyId, L"styles", L"patterns" },
    { &UIA_IsTransformPattern2AvailablePropertyId, L"transform2", L"patterns" },
    { &UIA_IsTextChildPatternAvailablePropertyId, L"textChild", L"patterns" },
    { &UIA_IsTextPattern2AvailablePropertyId, L"text2", L"patterns" },
};

static
PWSTR
ZpUia_CopyText(
    _In_ PCWSTR Text)
{
    SIZE_T Length = wcslen(Text) + 1;
    PWSTR Copy = Mem_Alloc(Length * sizeof(WCHAR));

    if (Copy != NULL) RtlCopyMemory(Copy, Text, Length * sizeof(WCHAR));
    return Copy;
}

static
PWSTR
ZpUia_FormatVariant(
    _In_ const VARIANT* Value,
    _Inout_ PULONG Flags)
{
    WCHAR Text[128];
    PWSTR Result;
    LONG Lower, Upper, Index;
    SIZE_T Offset, Capacity;
    HRESULT Status;

    switch (V_VT(Value))
    {
        case VT_EMPTY:
        case VT_NULL:
            return ZpUia_CopyText(L"");
        case VT_BSTR:
            return ZpUia_CopyText(V_BSTR(Value) == NULL ? L"" : V_BSTR(Value));
        case VT_BOOL:
            return ZpUia_CopyText(V_BOOL(Value) == VARIANT_TRUE ? L"true" : L"false");
        case VT_I4:
            _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%ld", V_I4(Value));
            return ZpUia_CopyText(Text);
        case VT_UI4:
            _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%lu", V_UI4(Value));
            return ZpUia_CopyText(Text);
        case VT_I8:
            _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%lld", V_I8(Value));
            return ZpUia_CopyText(Text);
        case VT_UI8:
            _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%llu", V_UI8(Value));
            return ZpUia_CopyText(Text);
        case VT_R8:
            _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%.6g", V_R8(Value));
            return ZpUia_CopyText(Text);
        case VT_UNKNOWN:
            *Flags |= ZP_UIA_PROPERTY_NOT_SUPPORTED;
            return ZpUia_CopyText(L"");
    }
    if (!(V_VT(Value) & VT_ARRAY) || V_ARRAY(Value) == NULL)
    {
        *Flags |= ZP_UIA_PROPERTY_UNAVAILABLE;
        return ZpUia_CopyText(L"");
    }
    Status = SafeArrayGetLBound(V_ARRAY(Value), 1, &Lower);
    if (SUCCEEDED(Status)) Status = SafeArrayGetUBound(V_ARRAY(Value), 1, &Upper);
    if (FAILED(Status))
    {
        *Flags |= ZP_UIA_PROPERTY_UNAVAILABLE;
        return ZpUia_CopyText(L"");
    }
    if (Upper < Lower) return ZpUia_CopyText(L"[]");
    Capacity = (SIZE_T)(Upper - Lower + 1) * 32 + 3;
    Result = Mem_Alloc(Capacity * sizeof(WCHAR));
    if (Result == NULL) return NULL;
    Result[0] = L'[';
    Result[1] = UNICODE_NULL;
    Offset = 1;
    for (Index = Lower; Index <= Upper; Index++)
    {
        if ((V_VT(Value) & VT_TYPEMASK) == VT_I4)
        {
            LONG Number;

            Status = SafeArrayGetElement(V_ARRAY(Value), &Index, &Number);
            if (SUCCEEDED(Status))
                _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%ld", Number);
        }
        else if ((V_VT(Value) & VT_TYPEMASK) == VT_R8)
        {
            DOUBLE Number;

            Status = SafeArrayGetElement(V_ARRAY(Value), &Index, &Number);
            if (SUCCEEDED(Status))
                _snwprintf_s(Text, ARRAYSIZE(Text), _TRUNCATE, L"%.6g", Number);
        }
        else
        {
            Status = E_NOTIMPL;
        }
        if (FAILED(Status))
        {
            Mem_Free(Result);
            *Flags |= ZP_UIA_PROPERTY_UNAVAILABLE;
            return ZpUia_CopyText(L"");
        }
        if (Index != Lower) Result[Offset++] = L',';
        Offset += _snwprintf_s(Result + Offset, Capacity - Offset, _TRUNCATE, L"%s", Text);
    }
    Result[Offset++] = L']';
    Result[Offset] = UNICODE_NULL;
    return Result;
}

static
ZP_STATUS
ZpAdministration_QueryUiAutomationProperties(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    IUIAutomation* Automation = NULL;
    IUIAutomationTreeWalker* Walker = NULL;
    IUIAutomationElement* Element = NULL;
    VARIANT Value;
    PWSTR Path, Text;
    ULONG Flags, Index;
    HRESULT Result, InitializeResult;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Identity->Length < 4 || Identity->Length >= 512)
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Path = ZpAdministration_CopyView(Identity);
    if (Path == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        Mem_Free(Path);
        return ZpStatus_FromCode(ZpStatusHResult, InitializeResult);
    }
    Result = CoCreateInstance(&CLSID_CUIAutomation,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IUIAutomation,
                              (PVOID*)&Automation);
    if (SUCCEEDED(Result)) Result = IUIAutomation_get_ControlViewWalker(Automation, &Walker);
    if (SUCCEEDED(Result)) Element = ZpUia_ResolveElement(Automation, Walker, Path);
    if (Element == NULL && SUCCEEDED(Result)) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (Index = 0; SUCCEEDED(Result) && NT_SUCCESS(Status) && Index < ARRAYSIZE(ZpUiaProperties); Index++)
    {
        VariantInit(&Value);
        Flags = 0;
        Result = IUIAutomationElement_GetCurrentPropertyValue(Element, *ZpUiaProperties[Index].Id, &Value);
        if (FAILED(Result))
        {
            Text = ZpUia_CopyText(L"");
            Flags = ZP_UIA_PROPERTY_UNAVAILABLE;
            Result = S_OK;
        }
        else
        {
            Text = ZpUia_FormatVariant(&Value, &Flags);
        }
        if (Text == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else
        {
            Status = ZpAdministration_AddRecord(&Builder,
                                                 ZpAdministrationKindUiAutomationProperty,
                                                 *ZpUiaProperties[Index].Id,
                                                 Flags,
                                                 0,
                                                 ZpUiaProperties[Index].Name,
                                                 NULL,
                                                 ZpUiaProperties[Index].Group,
                                                 Text);
            Mem_Free(Text);
        }
        VariantClear(&Value);
    }
    if (Element != NULL) IUIAutomationElement_Release(Element);
    if (Walker != NULL) IUIAutomationTreeWalker_Release(Walker);
    if (Automation != NULL) IUIAutomation_Release(Automation);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    Mem_Free(Path);
    if (SUCCEEDED(Result) && NT_SUCCESS(Status))
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) : ZpStatus_FromNtStatus(Status);
}
