#include "UnitTest.h"

#include "../KNSoft.ZPigeon.Client.SDK/Core/Snapshot.h"
#include "../Modules/Browser/Client.h"

#include <strsafe.h>

static
VOID
TestBrowserJson(
    PUNITTEST_RESULT TEST_PARAMETER_RESULT)
{
    ZP_CLIENT_OBJECT Client = { 0 };
    WCHAR TempPath[MAX_PATH], UserData[MAX_PATH], ProfilePath[MAX_PATH], FilePath[MAX_PATH];
    CHAR Text[32768];
    BYTE Request[MAX_PATH * sizeof(WCHAR) + 128];
    PBYTE Response = NULL;
    ULONG RequestLength, ResponseLength, Length, Index, PageIndex, Offset, SnapshotId, FirstId = 0, ArrayId;
    DWORD Written;
    HANDLE File;
    ZP_STATUS Status;
    ZP_BROWSER_DOCUMENT_PAGE_VIEW Page;
    ZP_BROWSER_DOCUMENT_NODE_VIEW Node;
    ZP_BROWSER_KIND Kind;
    UINT TempLength;

    InitializeListHead(&Client.Snapshots);
    TempLength = GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    TEST_OK(TempLength != 0 && TempLength < ARRAYSIZE(TempPath));
    if (TempLength == 0 || TempLength >= ARRAYSIZE(TempPath)) return;
    if (GetTempFileNameW(TempPath, L"ZPJ", 0, UserData) == 0)
    {
        TEST_OK(FALSE);
        return;
    }
    TEST_OK(DeleteFileW(UserData));
    if (!CreateDirectoryW(UserData, NULL))
    {
        TEST_OK(FALSE);
        return;
    }
    StringCchPrintfW(ProfilePath, ARRAYSIZE(ProfilePath), L"%s\\Fixture", UserData);
    if (!CreateDirectoryW(ProfilePath, NULL))
    {
        TEST_OK(FALSE);
        TEST_OK(RemoveDirectoryW(UserData));
        return;
    }
    for (Kind = ZpBrowserKindBookmark; Kind <= ZpBrowserKindSetting; Kind++)
    {
        // More than 256 nodes exercises snapshot storage growth and subsequent ID reuse.
        Text[0] = Kind == ZpBrowserKindBookmark ? '[' : '{';
        Length = 1;
        for (Index = 0; Index < 300; Index++)
        {
            if (Index != 0) Text[Length++] = ',';
            if (Kind == ZpBrowserKindSetting)
            {
                StringCchPrintfA(Text + Length, ARRAYSIZE(Text) - Length, "\"k%lu\":", Index);
                Length += (ULONG)strlen(Text + Length);
            }
            RtlCopyMemory(Text + Length, "{\"items\":[true,null,\"x\"],\"empty\":{},\"number\":1}",
                          sizeof("{\"items\":[true,null,\"x\"],\"empty\":{},\"number\":1}") - 1);
            Length += sizeof("{\"items\":[true,null,\"x\"],\"empty\":{},\"number\":1}") - 1;
        }
        Text[Length++] = Kind == ZpBrowserKindBookmark ? ']' : '}';
        StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%s", ProfilePath,
                         Kind == ZpBrowserKindBookmark ? L"Bookmarks" : L"Preferences");
        File = CreateFileW(FilePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
        TEST_OK(File != INVALID_HANDLE_VALUE);
        if (File == INVALID_HANDLE_VALUE) break;
        TEST_OK(WriteFile(File, Text, Length, &Written, NULL) && Written == Length);
        NtClose(File);
        TEST_OK(NT_SUCCESS(ZpBrowser_EncodeQuery(ZpBrowserChrome, Kind, L"Fixture", _STR_LEN(L"Fixture"),
                    UserData, (ULONG)wcslen(UserData), 0, 100, Request, sizeof(Request), &RequestLength)));
        Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_OPEN_DOCUMENT, Request, RequestLength,
                                   &Response, &ResponseLength);
        TEST_OK(ZpStatus_IsSuccess(Status));
        if (!ZpStatus_IsSuccess(Status)) goto FileCleanup;
        SnapshotId = 0;
        for (PageIndex = 0; PageIndex < 3; PageIndex++)
        {
            if (PageIndex != 0)
            {
                TEST_OK(NT_SUCCESS(ZpBrowser_EncodeDocumentQuery(SnapshotId, 1, PageIndex * 100, 100,
                                    Request, sizeof(Request), &RequestLength)));
                Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE,
                                           Request, RequestLength, &Response, &ResponseLength);
                TEST_OK(ZpStatus_IsSuccess(Status));
                if (!ZpStatus_IsSuccess(Status)) goto FileCleanup;
            }
            TEST_OK(NT_SUCCESS(ZpBrowser_DecodeDocumentPage(Response, ResponseLength, &Page)));
            TEST_OK(Page.Count == 100 && Page.NextCursor == (PageIndex == 2 ? 0 : (PageIndex + 1) * 100));
            TEST_OK(Page.ParentType == (Kind == ZpBrowserKindBookmark ?
                                         ZpBrowserDocumentArray : ZpBrowserDocumentObject));
            SnapshotId = Page.SnapshotId;
            Offset = 0;
            TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)));
            TEST_OK(Node.Type == ZpBrowserDocumentObject && FlagOn(Node.Flags, ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN));
            if (PageIndex == 0) FirstId = Node.Id;
            Mem_Free(Response);
            Response = NULL;
        }
        TEST_OK(NT_SUCCESS(ZpBrowser_EncodeDocumentQuery(SnapshotId, 1, 0, 1,
                            Request, sizeof(Request), &RequestLength)));
        Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE,
                                   Request, RequestLength, &Response, &ResponseLength);
        TEST_OK(ZpStatus_IsSuccess(Status));
        if (!ZpStatus_IsSuccess(Status)) goto FileCleanup;
        TEST_OK(NT_SUCCESS(ZpBrowser_DecodeDocumentPage(Response, ResponseLength, &Page)));
        Offset = 0;
        TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)) && Node.Id == FirstId);
        Mem_Free(Response);
        Response = NULL;
        TEST_OK(NT_SUCCESS(ZpBrowser_EncodeDocumentQuery(SnapshotId, FirstId, 0, 100,
                            Request, sizeof(Request), &RequestLength)));
        Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE,
                                   Request, RequestLength, &Response, &ResponseLength);
        TEST_OK(ZpStatus_IsSuccess(Status));
        if (!ZpStatus_IsSuccess(Status)) goto FileCleanup;
        TEST_OK(NT_SUCCESS(ZpBrowser_DecodeDocumentPage(Response, ResponseLength, &Page)) && Page.Count == 3);
        Offset = 0;
        ArrayId = 0;
        for (Index = 0; Index < Page.Count; Index++)
        {
            TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)));
            if (Node.Type == ZpBrowserDocumentArray) ArrayId = Node.Id;
            else if (Node.Type == ZpBrowserDocumentObject) TEST_OK(Node.Flags == 0);
            else TEST_OK(Node.Type == ZpBrowserDocumentNumber && Node.Value.Length == 1);
        }
        TEST_OK(ArrayId != 0);
        Mem_Free(Response);
        Response = NULL;
        TEST_OK(NT_SUCCESS(ZpBrowser_EncodeDocumentQuery(SnapshotId, ArrayId, 0, 100,
                            Request, sizeof(Request), &RequestLength)));
        Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE,
                                   Request, RequestLength, &Response, &ResponseLength);
        TEST_OK(ZpStatus_IsSuccess(Status));
        if (!ZpStatus_IsSuccess(Status)) goto FileCleanup;
        TEST_OK(NT_SUCCESS(ZpBrowser_DecodeDocumentPage(Response, ResponseLength, &Page)) && Page.Count == 3);
        Offset = 0;
        TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)) &&
                Node.Type == ZpBrowserDocumentBoolean);
        TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)) && Node.Type == ZpBrowserDocumentNull);
        TEST_OK(NT_SUCCESS(ZpBrowser_GetNextDocumentNode(&Page, &Offset, &Node)) &&
                Node.Type == ZpBrowserDocumentString);
        Mem_Free(Response);
        Response = NULL;
        TEST_OK(NT_SUCCESS(ZpBrowser_EncodeDocumentClose(SnapshotId, Request, sizeof(Request), &RequestLength)));
        Status = ZpBrowser_Execute(&Client, ZP_BROWSER_OPERATION_CLOSE_DOCUMENT,
                                   Request, RequestLength, &Response, &ResponseLength);
        TEST_OK(ZpStatus_IsSuccess(Status) && IsListEmpty(&Client.Snapshots));
FileCleanup:
        Mem_Free(Response);
        Response = NULL;
        ZpClientSnapshot_CloseAll(&Client);
        TEST_OK(DeleteFileW(FilePath));
    }
    TEST_OK(RemoveDirectoryW(ProfilePath));
    TEST_OK(RemoveDirectoryW(UserData));
}

TEST_FUNC(BrowserJson)
{
    UNREFERENCED_PARAMETER(TEST_PARAMETER_ARGC);
    UNREFERENCED_PARAMETER(TEST_PARAMETER_ARGV);
    TestBrowserJson(TEST_PARAMETER_RESULT);
}
