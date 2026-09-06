#include "UnitTest.h"

#include "../KNSoft.ZPigeon.Client.SDK/Core/Json.h"

TEST_FUNC(CoreJson)
{
    static const BYTE JsonText[] =
        "{\"text\":\"\xE4\xB8\xAD\",\"items\":[true,null,12.5],\"empty\":{}}!";
    static const BYTE InvalidUtf8[] = { '"', 0xC0, 0xAF, '"' };
    static const BYTE InvalidJson[] = { '{', '}', 'x' };
    static const BYTE ObjectText[] = "{\"key\":true}ignored";
    PZP_JSON_VALUE Root = NULL, Container = NULL, Element = NULL;
    PZP_JSON_ITERATOR Iterator = NULL;
    ZP_JSON_TYPE Type;
    WCHAR TempPath[MAX_PATH], FilePath[MAX_PATH];
    HSTRING String = NULL;
    PCWSTR Text = NULL;
    HANDLE File;
    DWORD Written;
    UINT TempResult;
    UINT32 Length;
    ULONG Size;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(TEST_PARAMETER_ARGC);
    UNREFERENCED_PARAMETER(TEST_PARAMETER_ARGV);

    Status = ZpJson_ParseUtf8(JsonText, sizeof(JsonText) - 2, &Root);
    TEST_OK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        TEST_OK(NT_SUCCESS(ZpJson_GetType(Root, &Type)) && Type == ZpJsonObject);
        TEST_OK(NT_SUCCESS(ZpJson_GetSize(Root, &Size)) && Size == 3);
        Status = ZpJson_GetNamedValue(Root, L"text", ARRAYSIZE(L"text") - 1, &Element);
        TEST_OK(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            Status = ZpJson_GetString(Element, &String);
            if (NT_SUCCESS(Status)) Text = WindowsGetStringRawBuffer(String, &Length);
            TEST_OK(NT_SUCCESS(Status) && Length == 1 && Text[0] == L'\x4E2D' && Text[1] == UNICODE_NULL);
            WindowsDeleteString(String);
            String = NULL;
            ZpJson_CloseValue(Element);
            Element = NULL;
        }
        TEST_OK(ZpJson_GetNamedValue(Root,
                                     L"missing",
                                     ARRAYSIZE(L"missing") - 1,
                                     &Element) == STATUS_NOT_FOUND);
        Status = ZpJson_GetNamedValue(Root, L"items", ARRAYSIZE(L"items") - 1, &Container);
        TEST_OK(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            TEST_OK(NT_SUCCESS(ZpJson_GetSize(Container, &Size)) && Size == 3);
            Status = ZpJson_CreateIterator(Container, 1, &Iterator);
            TEST_OK(NT_SUCCESS(Status));
            if (NT_SUCCESS(Status))
            {
                Status = ZpJson_IteratorNext(Iterator, &String, &Element);
                TEST_OK(NT_SUCCESS(Status) && String == NULL &&
                        NT_SUCCESS(ZpJson_GetType(Element, &Type)) && Type == ZpJsonNull);
                if (NT_SUCCESS(Status))
                {
                    ZpJson_CloseValue(Element);
                    Element = NULL;
                }
                Status = ZpJson_IteratorNext(Iterator, &String, &Element);
                TEST_OK(NT_SUCCESS(Status) && String == NULL);
                if (NT_SUCCESS(Status))
                {
                    Status = ZpJson_Stringify(Element, &String);
                    if (NT_SUCCESS(Status)) Text = WindowsGetStringRawBuffer(String, &Length);
                    TEST_OK(NT_SUCCESS(Status) && Length == ARRAYSIZE(L"12.5") - 1 &&
                            wcscmp(Text, L"12.5") == 0);
                    WindowsDeleteString(String);
                    String = NULL;
                    ZpJson_CloseValue(Element);
                    Element = NULL;
                }
                TEST_OK(ZpJson_IteratorNext(Iterator, &String, &Element) == STATUS_NO_MORE_ENTRIES);
                ZpJson_CloseIterator(Iterator);
                Iterator = NULL;
            }
            ZpJson_CloseValue(Container);
            Container = NULL;
        }
        ZpJson_CloseValue(Root);
        Root = NULL;
    }
    TEST_OK(!NT_SUCCESS(ZpJson_ParseUtf8(InvalidUtf8, sizeof(InvalidUtf8), &Root)));
    TEST_OK(ZpJson_ParseUtf8(InvalidJson, sizeof(InvalidJson), &Root) == STATUS_DATA_ERROR);
    TEST_OK(ZpJson_ParseUtf8(JsonText, 0, &Root) == STATUS_DATA_ERROR);
    Status = ZpJson_ParseUtf8(ObjectText, sizeof("{\"key\":true}") - 1, &Root);
    TEST_OK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        Status = ZpJson_CreateIterator(Root, 0, &Iterator);
        TEST_OK(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            Status = ZpJson_IteratorNext(Iterator, &String, &Element);
            if (NT_SUCCESS(Status)) Text = WindowsGetStringRawBuffer(String, &Length);
            TEST_OK(NT_SUCCESS(Status) && Length == ARRAYSIZE(L"key") - 1 && wcscmp(Text, L"key") == 0 &&
                    NT_SUCCESS(ZpJson_GetType(Element, &Type)) && Type == ZpJsonBoolean);
            if (NT_SUCCESS(Status))
            {
                WindowsDeleteString(String);
                String = NULL;
                ZpJson_CloseValue(Element);
            }
            TEST_OK(ZpJson_IteratorNext(Iterator, &String, &Element) == STATUS_NO_MORE_ENTRIES);
            ZpJson_CloseIterator(Iterator);
        }
    }
    ZpJson_CloseValue(Root);
    Root = NULL;

    Length = GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    TEST_OK(Length != 0 && Length < ARRAYSIZE(TempPath));
    if (Length != 0 && Length < ARRAYSIZE(TempPath))
    {
        TempResult = GetTempFileNameW(TempPath, L"ZPJ", 0, FilePath);
        TEST_OK(TempResult != 0);
        if (TempResult != 0)
        {
            File = CreateFileW(FilePath,
                               GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY,
                               NULL);
            TEST_OK(File != INVALID_HANDLE_VALUE);
            if (File != INVALID_HANDLE_VALUE)
            {
                Status = WriteFile(File, JsonText, sizeof(JsonText) - 2, &Written, NULL) &&
                         Written == sizeof(JsonText) - 2 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                NtClose(File);
                TEST_OK(NT_SUCCESS(Status));
                if (NT_SUCCESS(Status))
                {
                    Status = ZpJson_ParseUtf8File(FilePath, sizeof(JsonText) - 2, &Root);
                    TEST_OK(NT_SUCCESS(Status) &&
                            NT_SUCCESS(ZpJson_GetType(Root, &Type)) && Type == ZpJsonObject);
                    if (NT_SUCCESS(Status))
                    {
                        ZpJson_CloseValue(Root);
                        Root = NULL;
                    }
                    TEST_OK(ZpJson_ParseUtf8File(FilePath,
                                                 sizeof(JsonText) - 3,
                                                 &Root) == STATUS_FILE_TOO_LARGE);
                }
            }
            TEST_OK(DeleteFileW(FilePath));
        }
    }
}
