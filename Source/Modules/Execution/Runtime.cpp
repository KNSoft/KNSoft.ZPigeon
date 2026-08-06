#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "Runtime.h"

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string_view>
#include <vector>
#include <winver.h>

#pragma comment(lib, "Version.lib")

namespace
{
    bool IsPythonExecutionAlias(const std::wstring& path)
    {
        std::wstring value(path);
        std::transform(value.begin(), value.end(), value.begin(), towlower);
        return value.find(L"\\microsoft\\windowsapps\\") != std::wstring::npos;
    }

    std::wstring QueryRegistryString(HKEY key, PCWSTR name)
    {
        DWORD type, length = 0;
        LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &length);
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || length < sizeof(WCHAR)) return {};
        std::vector<WCHAR> value(length / sizeof(WCHAR));
        result = RegQueryValueExW(key,
                                  name,
                                  nullptr,
                                  &type,
                                  reinterpret_cast<PBYTE>(value.data()),
                                  &length);
        if (result != ERROR_SUCCESS) return {};
        value.back() = UNICODE_NULL;
        if (type != REG_EXPAND_SZ) return value.data();
        DWORD expandedLength = ExpandEnvironmentStringsW(value.data(), nullptr, 0);
        if (expandedLength == 0) return {};
        std::vector<WCHAR> expanded(expandedLength);
        return ExpandEnvironmentStringsW(value.data(), expanded.data(), expandedLength) == expandedLength ?
                   std::wstring(expanded.data()) : std::wstring();
    }

    std::wstring FindRegisteredPython(HKEY root, REGSAM view)
    {
        HKEY python;
        if (RegOpenKeyExW(root, L"Software\\Python", 0, KEY_ENUMERATE_SUB_KEYS | view, &python) != ERROR_SUCCESS)
            return {};
        std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> pythonOwner(python, RegCloseKey);
        WCHAR company[256], tag[256];
        for (DWORD companyIndex = 0;; companyIndex++)
        {
            DWORD companyLength = ARRAYSIZE(company);
            LONG result = RegEnumKeyExW(python,
                                        companyIndex,
                                        company,
                                        &companyLength,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr);
            if (result == ERROR_NO_MORE_ITEMS) break;
            if (result != ERROR_SUCCESS) continue;
            HKEY companyKey;
            if (RegOpenKeyExW(python, company, 0, KEY_ENUMERATE_SUB_KEYS | view, &companyKey) != ERROR_SUCCESS)
                continue;
            std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> companyOwner(companyKey,
                                                                                              RegCloseKey);
            for (DWORD tagIndex = 0;; tagIndex++)
            {
                DWORD tagLength = ARRAYSIZE(tag);
                result = RegEnumKeyExW(companyKey,
                                       tagIndex,
                                       tag,
                                       &tagLength,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr);
                if (result == ERROR_NO_MORE_ITEMS) break;
                if (result != ERROR_SUCCESS) continue;
                std::wstring installPath(tag);
                installPath.append(L"\\InstallPath");
                HKEY install;
                if (RegOpenKeyExW(companyKey,
                                  installPath.c_str(),
                                  0,
                                  KEY_QUERY_VALUE | view,
                                  &install) != ERROR_SUCCESS)
                    continue;
                std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> installOwner(install,
                                                                                                  RegCloseKey);
                std::wstring executable = QueryRegistryString(install, L"ExecutablePath");
                if (executable.empty())
                {
                    executable = QueryRegistryString(install, nullptr);
                    if (!executable.empty())
                    {
                        if (executable.back() != L'\\') executable.push_back(L'\\');
                        executable.append(L"python.exe");
                    }
                }
                if (ZpRuntime::IsExistingFile(executable) && !IsPythonExecutionAlias(executable)) return executable;
            }
        }
        return {};
    }

    std::wstring SystemExecutable(PCWSTR relativePath)
    {
        std::vector<WCHAR> path(MAX_PATH);
        UINT length;
        for (;;)
        {
            length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
            if (length == 0) return {};
            if (length < path.size()) break;
            path.resize(static_cast<size_t>(length) + 1);
        }
        std::wstring value(path.data(), length);
        value.append(relativePath);
        return ZpRuntime::IsExistingFile(value) ? value : std::wstring();
    }

    void QueryWindowsPowerShellVersion(USHORT (&version)[4])
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"Software\\Microsoft\\PowerShell\\3\\PowerShellEngine",
                          0,
                          KEY_QUERY_VALUE,
                          &key) != ERROR_SUCCESS)
            return;
        std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> owner(key, RegCloseKey);
        const std::wstring value = QueryRegistryString(key, L"PowerShellVersion");
        USHORT parsed[ARRAYSIZE(version)] = { 0 };
        PCWSTR cursor = value.c_str();
        ULONG index = 0;
        while (index < ARRAYSIZE(parsed) && *cursor != UNICODE_NULL)
        {
            PWSTR end;
            const ULONG part = wcstoul(cursor, &end, 10);
            if (end == cursor || part > MAXUSHORT || (*end != L'.' && *end != UNICODE_NULL)) return;
            parsed[index++] = static_cast<USHORT>(part);
            cursor = *end == L'.' ? end + 1 : end;
        }
        if (*cursor != UNICODE_NULL || index < 2) return;
        RtlCopyMemory(version, parsed, sizeof(parsed));
    }

    HRESULT QueryImage(PCWSTR path, PZP_EXECUTION_IMAGE_INFO image, bool queryVersion)
    {
        DWORD handle, length;
        PVOID data;
        VS_FIXEDFILEINFO* version;
        UINT versionLength;
        HANDLE file, mapping;
        PVOID view;
        PIMAGE_NT_HEADERS headers;
        HRESULT result = S_OK;

        RtlZeroMemory(image, sizeof(*image));
        if (queryVersion)
        {
            length = GetFileVersionInfoSizeW(path, &handle);
            if (length != 0)
            {
                data = Mem_Alloc(length);
                if (data == nullptr) return E_OUTOFMEMORY;
                if (GetFileVersionInfoW(path, 0, length, data) &&
                    VerQueryValueW(data, L"\\", reinterpret_cast<PVOID*>(&version), &versionLength) &&
                    versionLength >= sizeof(*version) && version->dwSignature == VS_FFI_SIGNATURE)
                {
                    image->Version[0] = HIWORD(version->dwProductVersionMS);
                    image->Version[1] = LOWORD(version->dwProductVersionMS);
                    image->Version[2] = HIWORD(version->dwProductVersionLS);
                    image->Version[3] = LOWORD(version->dwProductVersionLS);
                }
                Mem_Free(data);
            }
        }
        file = CreateFileW(path,
                           FILE_READ_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
        if (mapping == nullptr)
        {
            result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(file);
            return result;
        }
        view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr)
        {
            result = HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            headers = RtlImageNtHeader(view);
            if (headers == nullptr)
            {
                result = HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
            }
            else
            {
                image->Machine = headers->FileHeader.Machine;
                image->Subsystem = headers->OptionalHeader.Subsystem;
            }
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
        CloseHandle(file);
        return result;
    }

    std::wstring SiblingExecutable(const std::wstring& path, PCWSTR name)
    {
        const size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return {};
        std::wstring value(path, 0, separator + 1);
        value.append(name);
        return ZpRuntime::IsExistingFile(value) ? value : std::wstring();
    }

    struct RuntimeCandidate
    {
        BYTE Kind;
        std::wstring Path;
    };
}

bool ZpRuntime::IsExistingFile(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !FlagOn(attributes, FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ZpRuntime::FindPathExecutable(PCWSTR fileName, bool rejectPythonAlias)
{
    DWORD environmentLength = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (environmentLength == 0) return {};
    std::vector<WCHAR> environment(environmentLength);
    if (GetEnvironmentVariableW(L"PATH", environment.data(), environmentLength) + 1 != environmentLength) return {};
    std::wstring_view paths(environment.data());
    size_t offset = 0;
    while (offset <= paths.size())
    {
        const size_t separator = paths.find(L';', offset);
        std::wstring directory(paths.substr(offset,
                                            separator == std::wstring_view::npos ?
                                                paths.size() - offset : separator - offset));
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"')
            directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty())
        {
            const DWORD expandedLength = ExpandEnvironmentStringsW(directory.c_str(), nullptr, 0);
            if (expandedLength != 0)
            {
                std::vector<WCHAR> expanded(expandedLength);
                if (ExpandEnvironmentStringsW(directory.c_str(), expanded.data(), expandedLength) == expandedLength)
                {
                    std::wstring candidate(expanded.data());
                    if (candidate.back() != L'\\') candidate.push_back(L'\\');
                    candidate.append(fileName);
                    if (IsExistingFile(candidate) && (!rejectPythonAlias || !IsPythonExecutionAlias(candidate)))
                    {
                        const DWORD fullLength = GetFullPathNameW(candidate.c_str(), 0, nullptr, nullptr);
                        if (fullLength == 0) return candidate;
                        std::vector<WCHAR> full(fullLength);
                        if (GetFullPathNameW(candidate.c_str(), fullLength, full.data(), nullptr) != 0)
                            return full.data();
                        return candidate;
                    }
                }
            }
        }
        if (separator == std::wstring_view::npos) break;
        offset = separator + 1;
    }
    return {};
}

std::wstring ZpRuntime::FindPython()
{
    std::wstring path = FindPathExecutable(L"python.exe", true);
    if (!path.empty()) return path;
    path = FindRegisteredPython(HKEY_CURRENT_USER, 0);
    if (!path.empty()) return path;
    path = FindRegisteredPython(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    return path.empty() ? FindRegisteredPython(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY) : path;
}

HRESULT
ZpRuntime_QueryImage(
    _In_ PCWSTR Path,
    _Out_ PZP_EXECUTION_IMAGE_INFO Image)
{
    return QueryImage(Path, Image, true);
}

HRESULT
ZpRuntime_Enumerate(
    _In_ ZP_RUNTIME_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    std::vector<RuntimeCandidate> values;
    std::wstring python;

    if (Callback == nullptr) return E_INVALIDARG;
    values.reserve(10);
    values.push_back({ ZpExecutionRuntimeCommandPrompt, SystemExecutable(L"\\cmd.exe") });
    values.push_back({ ZpExecutionRuntimeWindowsPowerShell,
                       SystemExecutable(L"\\WindowsPowerShell\\v1.0\\powershell.exe") });
    values.push_back({ ZpExecutionRuntimeConsoleScriptHost, SystemExecutable(L"\\cscript.exe") });
    values.push_back({ ZpExecutionRuntimeWindowsScriptHost, SystemExecutable(L"\\wscript.exe") });
    values.push_back({ ZpExecutionRuntimeHtmlApplication, SystemExecutable(L"\\mshta.exe") });
    values.push_back({ ZpExecutionRuntimeNode, ZpRuntime::FindPathExecutable(L"node.exe") });
    python = ZpRuntime::FindPython();
    values.push_back({ ZpExecutionRuntimePython, python });
    values.push_back({ ZpExecutionRuntimePythonWindow, SiblingExecutable(python, L"pythonw.exe") });
    values.push_back({ ZpExecutionRuntimeGo, ZpRuntime::FindPathExecutable(L"go.exe") });
    values.push_back({ ZpExecutionRuntimePowerShell, ZpRuntime::FindPathExecutable(L"pwsh.exe") });
    for (const auto& value : values)
    {
        ZP_EXECUTION_IMAGE_INFO image;
        const bool queryVersion = value.Kind == ZpExecutionRuntimePowerShell ||
                                  value.Kind == ZpExecutionRuntimeNode ||
                                  value.Kind == ZpExecutionRuntimePython ||
                                  value.Kind == ZpExecutionRuntimePythonWindow ||
                                  value.Kind == ZpExecutionRuntimeGo;
        if (value.Path.empty()) continue;
        if (FAILED(QueryImage(value.Path.c_str(), &image, queryVersion))) RtlZeroMemory(&image, sizeof(image));
        if (value.Kind == ZpExecutionRuntimeWindowsPowerShell) QueryWindowsPowerShellVersion(image.Version);
        if (!Callback(value.Kind, value.Path.c_str(), &image, Context)) break;
    }
    return S_OK;
}
