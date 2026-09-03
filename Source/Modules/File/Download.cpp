#include "Download.h"

#include <Bits.h>
#include <Winhttp.h>
#include <wrl/client.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "Bits.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Winhttp.lib")

namespace
{
    constexpr size_t MaximumActiveJobs = 8;
    constexpr ULONG BitsRetryDelaySeconds = 60;
    constexpr ULONG BitsNoProgressTimeoutSeconds = 24 * 60 * 60;
    constexpr WCHAR BitsDisplayNamePrefix[] = L"KNSoft.ZPigeon.FileDownload/";

    struct DownloadJob
    {
        DownloadJob(GUID id, std::wstring url, std::wstring path, std::wstring temporaryPath,
                    ZP_FILE_DOWNLOAD_ENGINE engine, BYTE flags)
            : Id(id), Url(std::move(url)), Path(std::move(path)), TemporaryPath(std::move(temporaryPath)),
              Engine(engine), State(ZpFileDownloadQueued), Flags(flags), Result(S_OK), TransferredBytes(0),
              TotalBytes(MAXULONGLONG), CancelRequested(false)
        {
        }

        GUID Id;
        std::wstring Url;
        std::wstring Path;
        std::wstring TemporaryPath;
        std::wstring ErrorText;
        ZP_FILE_DOWNLOAD_ENGINE Engine;
        ZP_FILE_DOWNLOAD_STATE State;
        BYTE Flags;
        HRESULT Result;
        ULONGLONG TransferredBytes;
        ULONGLONG TotalBytes;
        bool CancelRequested;
    };

    struct HResultError
    {
        HRESULT Result;
        std::wstring Text;
    };

    class InternetHandle
    {
    public:
        InternetHandle() noexcept = default;
        explicit InternetHandle(HINTERNET value) noexcept : Value(value) {}
        ~InternetHandle()
        {
            if (Value != nullptr)
                WinHttpCloseHandle(Value);
        }
        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;
        HINTERNET get() const noexcept
        {
            return Value;
        }

    private:
        HINTERNET Value = nullptr;
    };

    class FileHandle
    {
    public:
        explicit FileHandle(HANDLE value) noexcept : Value(value) {}
        ~FileHandle()
        {
            if (Value != INVALID_HANDLE_VALUE)
                CloseHandle(Value);
        }
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;
        HANDLE get() const noexcept
        {
            return Value;
        }
        void close() noexcept
        {
            if (Value != INVALID_HANDLE_VALUE)
            {
                CloseHandle(Value);
                Value = INVALID_HANDLE_VALUE;
            }
        }

    private:
        HANDLE Value;
    };

    struct UrlParts
    {
        std::wstring Host;
        std::wstring Object;
        INTERNET_PORT Port;
        bool Secure;
    };

    std::mutex JobsLock;
    std::vector<std::shared_ptr<DownloadJob>> Jobs;
    std::once_flag BitsCleanupFlag;

    [[noreturn]] void Throw(HRESULT result, std::wstring text = {})
    {
        throw HResultError{result, std::move(text)};
    }

    void Check(BOOL result)
    {
        if (!result)
            Throw(HRESULT_FROM_WIN32(GetLastError()));
    }

    void Check(HRESULT result)
    {
        if (FAILED(result))
            Throw(result);
    }

    std::wstring GuidString(REFGUID id)
    {
        WCHAR value[39];

        if (StringFromGUID2(id, value, ARRAYSIZE(value)) == 0) Throw(E_FAIL);
        return { value + 1, 36 };
    }

    std::wstring ErrorMessage(HRESULT result)
    {
        PWSTR buffer = nullptr;
        DWORD length =
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<PWSTR>(&buffer), 0, nullptr);
        if (length == 0)
            return {};
        std::unique_ptr<WCHAR, decltype(&LocalFree)> owner(buffer, LocalFree);
        std::wstring message(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
            message.pop_back();
        return message;
    }

    UrlParts ParseUrl(const std::wstring& url)
    {
        if (url.find(L'#') != std::wstring::npos)
            Throw(E_INVALIDARG);
        URL_COMPONENTS components = {sizeof(components)};
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUserNameLength = static_cast<DWORD>(-1);
        components.dwPasswordLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        Check(WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components));
        if ((components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) ||
            components.dwHostNameLength == 0 || components.dwUserNameLength != 0 || components.dwPasswordLength != 0)
        {
            Throw(E_INVALIDARG);
        }
        std::wstring object;
        if (components.dwUrlPathLength != 0)
        {
            object.assign(components.lpszUrlPath, components.dwUrlPathLength);
        }
        else
            object = L"/";
        if (components.dwExtraInfoLength != 0)
        {
            object.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        }
        return {std::wstring(components.lpszHostName, components.dwHostNameLength), std::move(object), components.nPort,
                components.nScheme == INTERNET_SCHEME_HTTPS};
    }

    bool IsCanceled(const std::shared_ptr<DownloadJob>& job)
    {
        std::scoped_lock lock(JobsLock);
        return job->CancelRequested;
    }

    void UpdateProgress(const std::shared_ptr<DownloadJob>& job, ZP_FILE_DOWNLOAD_STATE state,
                        ULONGLONG transferredBytes, ULONGLONG totalBytes)
    {
        std::scoped_lock lock(JobsLock);
        job->State = state;
        job->TransferredBytes = transferredBytes;
        job->TotalBytes = totalBytes;
    }

    void CompleteFailure(const std::shared_ptr<DownloadJob>& job, HRESULT result, std::wstring errorText = {}) noexcept
    {
        if (errorText.empty())
        {
            try
            {
                errorText = ErrorMessage(result);
            }
            catch (...)
            {
                // The structured result remains available if diagnostic formatting fails.
            }
        }
        std::scoped_lock lock(JobsLock);
        job->State = job->CancelRequested ? ZpFileDownloadCanceled : ZpFileDownloadFailed;
        job->Result = job->CancelRequested ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : result;
        job->ErrorText = std::move(errorText);
    }

    void Commit(const std::shared_ptr<DownloadJob>& job)
    {
        std::scoped_lock lock(JobsLock);
        if (job->CancelRequested)
            Throw(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        DWORD flags = MOVEFILE_WRITE_THROUGH;
        if ((job->Flags & ZP_FILE_DOWNLOAD_FLAG_OVERWRITE) != 0)
            flags |= MOVEFILE_REPLACE_EXISTING;
        Check(MoveFileExW(job->TemporaryPath.c_str(), job->Path.c_str(), flags));
        job->State = ZpFileDownloadCompleted;
        job->Result = S_OK;
        job->TransferredBytes = job->TotalBytes == MAXULONGLONG ? job->TransferredBytes : job->TotalBytes;
        job->ErrorText.clear();
    }

    void CleanupStaleBitsJobs(IBackgroundCopyManager* manager) noexcept
    {
        Microsoft::WRL::ComPtr<IEnumBackgroundCopyJobs> jobs;
        if (FAILED(manager->EnumJobs(0, &jobs)))
            return;
        for (;;)
        {
            Microsoft::WRL::ComPtr<IBackgroundCopyJob> job;
            ULONG fetched;
            HRESULT result = jobs->Next(1, &job, &fetched);
            if (result != S_OK || fetched != 1)
                return;
            PWSTR displayName = nullptr;
            if (SUCCEEDED(job->GetDisplayName(&displayName)))
            {
                std::unique_ptr<WCHAR, decltype(&CoTaskMemFree)> owner(displayName, CoTaskMemFree);
                if (wcsncmp(displayName, BitsDisplayNamePrefix, ARRAYSIZE(BitsDisplayNamePrefix) - 1) == 0)
                {
                    job->Cancel();
                }
            }
        }
    }

    std::wstring BitsErrorText(IBackgroundCopyJob* job, HRESULT* result)
    {
        Microsoft::WRL::ComPtr<IBackgroundCopyError> error;
        BG_ERROR_CONTEXT context;
        Check(job->GetError(&error));
        Check(error->GetError(&context, result));
        PWSTR description = nullptr;
        if (FAILED(error->GetErrorDescription(GetUserDefaultUILanguage(), &description)))
            return {};
        std::unique_ptr<WCHAR, decltype(&CoTaskMemFree)> owner(description, CoTaskMemFree);
        return description;
    }

    void RunBits(const std::shared_ptr<DownloadJob>& download)
    {
        HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE)
            Throw(initializeResult);
        struct Apartment
        {
            HRESULT Result;
            ~Apartment()
            {
                if (SUCCEEDED(Result))
                    CoUninitialize();
            }
        } apartment{initializeResult};
        Microsoft::WRL::ComPtr<IBackgroundCopyManager> manager;
        Check(CoCreateInstance(CLSID_BackgroundCopyManager, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager)));
        std::call_once(BitsCleanupFlag, [&]() { CleanupStaleBitsJobs(manager.Get()); });
        Microsoft::WRL::ComPtr<IBackgroundCopyJob> job;
        GUID bitsId;
        std::wstring displayName = BitsDisplayNamePrefix + GuidString(download->Id);
        Check(manager->CreateJob(displayName.c_str(), BG_JOB_TYPE_DOWNLOAD, &bitsId, &job));
        bool finalized = false;
        try
        {
            Check(job->AddFile(download->Url.c_str(), download->TemporaryPath.c_str()));
            Check(job->SetMinimumRetryDelay(BitsRetryDelaySeconds));
            Check(job->SetNoProgressTimeout(BitsNoProgressTimeoutSeconds));
            Check(job->Resume());
            for (;;)
            {
                if (IsCanceled(download))
                    Throw(HRESULT_FROM_WIN32(ERROR_CANCELLED));
                BG_JOB_PROGRESS progress;
                Check(job->GetProgress(&progress));
                BG_JOB_STATE state;
                Check(job->GetState(&state));
                if (state == BG_JOB_STATE_TRANSFERRED)
                {
                    Check(job->Complete());
                    finalized = true;
                    Commit(download);
                    return;
                }
                if (state == BG_JOB_STATE_ERROR)
                {
                    HRESULT result;
                    std::wstring description = BitsErrorText(job.Get(), &result);
                    Throw(result, std::move(description));
                }
                if (state == BG_JOB_STATE_CANCELLED || state == BG_JOB_STATE_ACKNOWLEDGED)
                {
                    Throw(HRESULT_FROM_WIN32(ERROR_CANCELLED));
                }
                UpdateProgress(download,
                               state == BG_JOB_STATE_TRANSIENT_ERROR ? ZpFileDownloadTransientError
                                                                     : ZpFileDownloadTransferring,
                               progress.BytesTransferred, progress.BytesTotal);
                Sleep(500);
            }
        }
        catch (...)
        {
            if (!finalized)
                job->Cancel();
            throw;
        }
    }

    void WriteAll(HANDLE file, const BYTE* buffer, DWORD length)
    {
        while (length != 0)
        {
            DWORD written;
            Check(WriteFile(file, buffer, length, &written, nullptr));
            if (written == 0)
                Throw(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
            buffer += written;
            length -= written;
        }
    }

    ULONGLONG ContentLength(HINTERNET request)
    {
        WCHAR value[32];
        DWORD length = sizeof(value);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, value, &length,
                                 WINHTTP_NO_HEADER_INDEX))
        {
            if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
                return MAXULONGLONG;
            Throw(HRESULT_FROM_WIN32(GetLastError()));
        }
        value[ARRAYSIZE(value) - 1] = UNICODE_NULL;
        WCHAR* end;
        ULONGLONG result = _wcstoui64(value, &end, 10);
        return end != value && *end == UNICODE_NULL ? result : MAXULONGLONG;
    }

    void RunWinHttp(const std::shared_ptr<DownloadJob>& download)
    {
        UrlParts url = ParseUrl(download->Url);
        InternetHandle session(WinHttpOpen(L"KNSoft.ZPigeon/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (session.get() == nullptr)
            Throw(HRESULT_FROM_WIN32(GetLastError()));
        Check(WinHttpSetTimeouts(session.get(), 30000, 30000, 30000, 30000));
        InternetHandle connection(WinHttpConnect(session.get(), url.Host.c_str(), url.Port, 0));
        if (connection.get() == nullptr)
            Throw(HRESULT_FROM_WIN32(GetLastError()));
        PCWSTR acceptTypes[] = {L"*/*", nullptr};
        InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", url.Object.c_str(), nullptr,
                                                  WINHTTP_NO_REFERER, acceptTypes,
                                                  url.Secure ? WINHTTP_FLAG_SECURE : 0));
        if (request.get() == nullptr)
            Throw(HRESULT_FROM_WIN32(GetLastError()));
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        Check(WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)));
        FileHandle file(CreateFileW(download->TemporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE)
            Throw(HRESULT_FROM_WIN32(GetLastError()));
        Check(WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
        Check(WinHttpReceiveResponse(request.get(), nullptr));
        DWORD statusCode;
        DWORD statusLength = sizeof(statusCode);
        Check(WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusLength, WINHTTP_NO_HEADER_INDEX));
        if (statusCode < 200 || statusCode >= 300)
        {
            Throw(HRESULT_FROM_WIN32(ERROR_WINHTTP_INVALID_SERVER_RESPONSE), L"HTTP " + std::to_wstring(statusCode));
        }
        ULONGLONG total = ContentLength(request.get());
        ULONGLONG transferred = 0;
        std::vector<BYTE> buffer(0x10000);
        for (;;)
        {
            if (IsCanceled(download))
                Throw(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            DWORD read;
            Check(WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read));
            if (read == 0)
                break;
            WriteAll(file.get(), buffer.data(), read);
            transferred += read;
            UpdateProgress(download, ZpFileDownloadTransferring, transferred, total);
        }
        if (total != MAXULONGLONG && transferred != total)
        {
            Throw(HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
        }
        Check(FlushFileBuffers(file.get()));
        file.close();
        Commit(download);
    }

    void CALLBACK RunDownload(PTP_CALLBACK_INSTANCE, PVOID context) noexcept
    {
        std::unique_ptr<std::shared_ptr<DownloadJob>> holder(static_cast<std::shared_ptr<DownloadJob>*>(context));
        std::shared_ptr<DownloadJob> job = *holder;
        try
        {
            if (job->Engine == ZpFileDownloadBits)
                RunBits(job);
            else
                RunWinHttp(job);
        }
        catch (const HResultError& error)
        {
            CompleteFailure(job, error.Result, error.Text);
        }
        catch (const std::bad_alloc&)
        {
            CompleteFailure(job, E_OUTOFMEMORY);
        }
        catch (...)
        {
            CompleteFailure(job, E_FAIL);
        }
        DeleteFileW(job->TemporaryPath.c_str());
    }

    std::wstring TemporaryPath(const std::wstring& path, REFGUID id)
    {
        size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            Throw(E_INVALIDARG);
        return path.substr(0, separator + 1) + L".zpigeon-" + GuidString(id) + L".tmp";
    }

    bool IsAbsolutePath(const std::wstring& path)
    {
        if (path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
            path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        {
            return true;
        }
        if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\' || path[2] == L'?' || path[2] == L'.')
        {
            return false;
        }
        size_t share = path.find(L'\\', 2);
        return share != std::wstring::npos && share != 2 && share + 1 < path.size();
    }

    NTSTATUS Invoke(const auto& operation) noexcept
    {
        try
        {
            operation();
            return STATUS_SUCCESS;
        }
        catch (const HResultError& error)
        {
            if (error.Result == E_OUTOFMEMORY)
                return STATUS_NO_MEMORY;
            return HRESULT_FACILITY(error.Result) == FACILITY_WIN32 ? NTSTATUS_FROM_WIN32(HRESULT_CODE(error.Result))
                                                                    : STATUS_INVALID_PARAMETER;
        }
        catch (const std::bad_alloc&)
        {
            return STATUS_NO_MEMORY;
        }
        catch (...)
        {
            return STATUS_UNSUCCESSFUL;
        }
    }
} // namespace

struct _ZP_FILE_DOWNLOAD_SNAPSHOT
{
    struct Value
    {
        GUID Id;
        std::wstring Url;
        std::wstring Path;
        std::wstring ErrorText;
        ZP_FILE_DOWNLOAD_ENGINE Engine;
        ZP_FILE_DOWNLOAD_STATE State;
        ULONG Result;
        ULONGLONG TransferredBytes;
        ULONGLONG TotalBytes;
    };

    std::vector<Value> Values;
    std::vector<ZP_FILE_DOWNLOAD_RECORD> Records;
};

NTSTATUS
ZpFileDownload_Start(_In_ ZP_FILE_DOWNLOAD_ENGINE Engine, _In_ BYTE Flags, _In_ const GUID* Id,
                     _In_reads_(UrlLength) PCWCH Url, _In_ ULONG UrlLength,
                     _In_reads_(PathLength) PCWCH Path, _In_ ULONG PathLength)
{
    return Invoke(
        [&]()
        {
            if ((Engine != ZpFileDownloadBits && Engine != ZpFileDownloadWinHttp) ||
                (Flags & ~ZP_FILE_DOWNLOAD_FLAG_OVERWRITE) != 0 || Id == nullptr ||
                Url == nullptr || UrlLength == 0 || UrlLength > ZP_FILE_DOWNLOAD_URL_MAX_LENGTH ||
                Path == nullptr || PathLength == 0 ||
                PathLength > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH)
            {
                Throw(E_INVALIDARG);
            }
            std::wstring url(Url, UrlLength);
            std::wstring path(Path, PathLength);
            if (url.find(UNICODE_NULL) != std::wstring::npos || path.find(UNICODE_NULL) != std::wstring::npos)
            {
                Throw(E_INVALIDARG);
            }
            ParseUrl(url);
            if (!IsAbsolutePath(path) || path.back() == L'\\' || path.back() == L'/')
                Throw(E_INVALIDARG);
            std::wstring temporaryPath = TemporaryPath(path, *Id);
            if (temporaryPath.size() > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH)
                Throw(E_INVALIDARG);
            DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                Throw(HRESULT_FROM_WIN32(ERROR_DIRECTORY));
            }
            if ((Flags & ZP_FILE_DOWNLOAD_FLAG_OVERWRITE) == 0 && attributes != INVALID_FILE_ATTRIBUTES)
            {
                Throw(HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
            }
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                DWORD error = GetLastError();
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                {
                    Throw(HRESULT_FROM_WIN32(error));
                }
            }
            auto job = std::make_shared<DownloadJob>(*Id, std::move(url), std::move(path),
                                                     std::move(temporaryPath), Engine, Flags);
            {
                std::scoped_lock lock(JobsLock);
                if (std::any_of(Jobs.begin(), Jobs.end(),
                                [&job](const auto& existing)
                                {
                                    return InlineIsEqualGUID(existing->Id, job->Id) ||
                                           (existing->State < ZpFileDownloadCompleted &&
                                            _wcsicmp(existing->Path.c_str(), job->Path.c_str()) == 0);
                                }))
                {
                    Throw(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
                }
                if (std::count_if(Jobs.begin(), Jobs.end(), [](const auto& existing)
                                  { return existing->State < ZpFileDownloadCompleted; }) >= MaximumActiveJobs)
                {
                    Throw(HRESULT_FROM_WIN32(ERROR_BUSY));
                }
                if (Jobs.size() == ZP_FILE_DOWNLOAD_MAX_COUNT)
                {
                    auto completed = std::find_if(Jobs.begin(), Jobs.end(), [](const auto& existing)
                                                  { return existing->State >= ZpFileDownloadCompleted; });
                    if (completed == Jobs.end())
                        Throw(HRESULT_FROM_WIN32(ERROR_BUSY));
                    Jobs.erase(completed);
                }
                Jobs.push_back(job);
            }
            auto context = new (std::nothrow) std::shared_ptr<DownloadJob>(job);
            if (context != nullptr && TrySubmitThreadpoolCallback(RunDownload, context, nullptr))
                return;
            DWORD error = context == nullptr ? ERROR_NOT_ENOUGH_MEMORY : GetLastError();
            delete context;
            std::scoped_lock lock(JobsLock);
            auto failed = std::find(Jobs.begin(), Jobs.end(), job);
            if (failed != Jobs.end())
                Jobs.erase(failed);
            Throw(HRESULT_FROM_WIN32(error));
        });
}

NTSTATUS
ZpFileDownload_Cancel(_In_ const GUID* Id)
{
    return Invoke(
        [&]()
        {
            if (Id == nullptr) Throw(E_INVALIDARG);
            std::scoped_lock lock(JobsLock);
            auto job = std::find_if(Jobs.begin(), Jobs.end(),
                                    [Id](const auto& value) { return InlineIsEqualGUID(value->Id, *Id); });
            if (job == Jobs.end())
                Throw(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
            if ((*job)->State >= ZpFileDownloadCompleted)
                Throw(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
            (*job)->CancelRequested = true;
        });
}

NTSTATUS
ZpFileDownload_CreateSnapshot(_Outptr_ PZP_FILE_DOWNLOAD_SNAPSHOT* Snapshot,
                              _Outptr_result_buffer_(*Count) PCZP_FILE_DOWNLOAD_RECORD* Records, _Out_ PULONG Count)
{
    return Invoke(
        [&]()
        {
            auto snapshot = std::make_unique<ZP_FILE_DOWNLOAD_SNAPSHOT>();
            {
                std::scoped_lock lock(JobsLock);
                snapshot->Values.reserve(Jobs.size());
                for (const auto& job : Jobs)
                {
                    snapshot->Values.push_back({job->Id, job->Url, job->Path, job->ErrorText, job->Engine, job->State,
                                                static_cast<ULONG>(job->Result), job->TransferredBytes,
                                                job->TotalBytes});
                }
            }
            snapshot->Records.reserve(snapshot->Values.size());
            for (const auto& value : snapshot->Values)
            {
                snapshot->Records.push_back({value.Engine, value.State, value.Result, value.TransferredBytes,
                                             value.TotalBytes, value.Id, value.Url.c_str(),
                                             static_cast<ULONG>(value.Url.size()),
                                             value.Path.c_str(), static_cast<ULONG>(value.Path.size()),
                                             value.ErrorText.c_str(), static_cast<ULONG>(value.ErrorText.size())});
            }
            *Records = snapshot->Records.data();
            *Count = static_cast<ULONG>(snapshot->Records.size());
            *Snapshot = snapshot.release();
        });
}

VOID ZpFileDownload_DestroySnapshot(_In_ PZP_FILE_DOWNLOAD_SNAPSHOT Snapshot)
{
    delete Snapshot;
}
