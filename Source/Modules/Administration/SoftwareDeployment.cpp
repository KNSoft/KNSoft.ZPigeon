#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "SoftwareDeployment.h"

#include "ProcessCapture.h"
#include "../Execution/Runtime.h"

#include <lmcons.h>
#include <shlwapi.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/Microsoft.Management.Deployment.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "shlwapi.lib")

namespace
{
    namespace Appx = winrt::Windows::Management::Deployment;
    namespace Json = winrt::Windows::Data::Json;
    namespace WinGet = winrt::Microsoft::Management::Deployment;

    using ZpRuntime::FindPathExecutable;
    using ZpRuntime::FindPython;
    using ZpRuntime::IsExistingFile;

    constexpr size_t MaximumJobs = 256;
    constexpr size_t MaximumPayloadValues = 34;
    constexpr ULONG MaximumPayloadValueLength = 32767;
    constexpr ULONG PackageEnumerationOutputLimit = 0x00800000;
    constexpr ULONG PackageEnumerationTimeout = 120000;
    constexpr ULONG PackageOperationOutputLimit = 0x00100000;
    constexpr ULONG PackageOperationTimeout = 3600000;
    constexpr GUID WinGetPackageManager =
        { 0xC53A4F16, 0x787E, 0x42A4, { 0xB3, 0x04, 0x29, 0xEF, 0xFB, 0x4B, 0xF5, 0x97 } };
    constexpr GUID WinGetFindPackagesOptions =
        { 0x572DED96, 0x9C60, 0x4526, { 0x8F, 0x92, 0xEE, 0x7D, 0x91, 0xD3, 0x8C, 0x1A } };
    constexpr GUID WinGetCompositeOptions =
        { 0x526534B8, 0x7E46, 0x47C8, { 0x84, 0x16, 0xB1, 0x68, 0x5C, 0x32, 0x7D, 0x37 } };
    constexpr GUID WinGetInstallOptions =
        { 0x1095F097, 0xEB96, 0x453B, { 0xB4, 0xE6, 0x16, 0x13, 0x63, 0x7F, 0x3B, 0x14 } };
    constexpr GUID WinGetUninstallOptions =
        { 0xE1D9A11E, 0x9F85, 0x4D87, { 0x9C, 0x17, 0x2B, 0x93, 0x14, 0x3A, 0xDB, 0x8D } };
    constexpr GUID WinGetPackageMatchFilter =
        { 0xD02C9DAF, 0x99DC, 0x429C, { 0xB5, 0x03, 0x4E, 0x50, 0x4E, 0x4A, 0xB0, 0x00 } };
    constexpr GUID WinGetAuthenticationArguments =
        { 0xBA580786, 0xBDE3, 0x4F6C, { 0xB8, 0xF3, 0x44, 0x69, 0x8A, 0xC8, 0x71, 0x1A } };

    struct DeploymentJob
    {
        DeploymentJob(
            std::wstring id,
            std::vector<std::wstring> payload,
            ZP_ADMINISTRATION_ACTION action,
            ULONG flags) :
            Id(std::move(id)),
            Payload(std::move(payload)),
            Action(action),
            State(ZP_SOFTWARE_DEPLOYMENT_STATE_QUEUED),
            Flags(flags),
            ErrorCode(S_OK),
            InstallerErrorCode(0),
            Progress(0),
            RebootRequired(false)
        {
        }

        std::wstring Id;
        std::wstring ErrorText;
        std::vector<std::wstring> Payload;
        ZP_ADMINISTRATION_ACTION Action;
        ULONG State;
        ULONG Flags;
        ULONG ErrorCode;
        ULONG InstallerErrorCode;
        ULONG Progress;
        bool RebootRequired;
    };

    std::mutex JobsLock;
    std::vector<std::shared_ptr<DeploymentJob>> Jobs;

    HRESULT RunExternalPackageCommand(const std::shared_ptr<DeploymentJob>& job, std::wstring* errorText);

    using DllGetClassObjectRoutine = HRESULT (WINAPI *)(REFCLSID, REFIID, LPVOID*);

    DllGetClassObjectRoutine GetWinGetClassObject()
    {
        static DllGetClassObjectRoutine routine = []
        {
            std::vector<WCHAR> path(MAX_PATH);
            DWORD length;

            for (;;)
            {
                length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0) winrt::throw_last_error();
                if (length < path.size() - 1) break;
                path.resize(path.size() * 2);
            }
            path.resize(length + 1);
            auto separator = std::find(path.rbegin(), path.rend(), L'\\');
            if (separator == path.rend()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME));
            path.resize(static_cast<size_t>(separator.base() - path.begin()));
            static constexpr WCHAR FileName[] = L"Microsoft.Management.Deployment.dll";
            path.insert(path.end(), FileName, FileName + ARRAYSIZE(FileName));
            HMODULE module = LoadLibraryExW(path.data(),
                                            nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (module == nullptr) winrt::throw_last_error();
            auto value = reinterpret_cast<DllGetClassObjectRoutine>(GetProcAddress(module, "DllGetClassObject"));
            if (value == nullptr) winrt::throw_last_error();
            return value;
        }();
        return routine;
    }

    template<typename T>
    T CreateWinGetObject(REFCLSID classId)
    {
        winrt::com_ptr<IClassFactory> factory;
        winrt::check_hresult(GetWinGetClassObject()(classId, IID_PPV_ARGS(factory.put())));
        void* instance;
        winrt::check_hresult(factory->CreateInstance(
            nullptr,
            reinterpret_cast<const GUID&>(winrt::guid_of<T>()),
            &instance));
        return T{ instance, winrt::take_ownership_from_abi };
    }

    std::wstring FindNpmCli(const std::wstring& node)
    {
        size_t separator = node.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return {};
        std::wstring path(node, 0, separator + 1);
        path.append(L"node_modules\\npm\\bin\\npm-cli.js");
        return IsExistingFile(path) ? path : std::wstring();
    }

    std::wstring Utf8Text(const BYTE* buffer, ULONG length)
    {
        if (length >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF)
        {
            buffer += 3;
            length -= 3;
        }
        if (length == 0) return {};
        int characters = MultiByteToWideChar(CP_UTF8,
                                              MB_ERR_INVALID_CHARS,
                                              reinterpret_cast<PCCH>(buffer),
                                              length,
                                              nullptr,
                                              0);
        if (characters == 0) winrt::throw_last_error();
        std::wstring text(static_cast<size_t>(characters), UNICODE_NULL);
        if (MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                reinterpret_cast<PCCH>(buffer),
                                length,
                                text.data(),
                                characters) != characters)
        {
            winrt::throw_last_error();
        }
        return text;
    }

    std::wstring RunProcessText(
        const std::wstring& application,
        const std::vector<std::wstring>& arguments,
        bool captureStandardError = false,
        ULONG timeout = PackageEnumerationTimeout,
        ULONG maximumOutput = PackageEnumerationOutputLimit,
        PULONG processExitCode = nullptr)
    {
        std::vector<PCWSTR> values;
        values.reserve(arguments.size());
        for (auto const& argument : arguments) values.push_back(argument.c_str());
        PBYTE output;
        ULONG outputLength, exitCode;
        NTSTATUS status = ZpAdministration_RunProcess(application.c_str(),
                                                       values.data(),
                                                       static_cast<ULONG>(values.size()),
                                                       maximumOutput,
                                                       timeout,
                                                       captureStandardError,
                                                       &output,
                                                       &outputLength,
                                                       &exitCode);
        if (!NT_SUCCESS(status)) winrt::throw_hresult(HRESULT_FROM_NT(status));
        std::unique_ptr<BYTE, decltype(&Mem_Free)> owner(output, Mem_Free);
        std::wstring text = Utf8Text(output, outputLength);
        if (processExitCode != nullptr)
        {
            *processExitCode = exitCode;
        }
        else if (exitCode != ERROR_SUCCESS)
        {
            winrt::throw_hresult(HRESULT_FROM_WIN32(exitCode));
        }
        while (!text.empty() && iswspace(text.back())) text.pop_back();
        return text;
    }

    std::wstring CurrentAccountName()
    {
        WCHAR name[UNLEN + 1];
        DWORD length = ARRAYSIZE(name);
        if (!GetUserNameW(name, &length)) winrt::throw_last_error();
        return name;
    }

    void UpdateJob(const std::shared_ptr<DeploymentJob>& job, ULONG state, ULONG progress)
    {
        std::scoped_lock lock(JobsLock);
        job->State = state;
        job->Progress = (std::min)(progress, 10000UL);
    }

    std::wstring ErrorMessage(HRESULT result)
    {
        PWSTR buffer = nullptr;
        DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,
                                      static_cast<DWORD>(result),
                                      0,
                                      reinterpret_cast<PWSTR>(&buffer),
                                      0,
                                      nullptr);
        if (length == 0) return {};
        std::unique_ptr<WCHAR, decltype(&LocalFree)> owner(buffer, LocalFree);
        std::wstring message(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
        return message;
    }

    void CompleteJob(
        const std::shared_ptr<DeploymentJob>& job,
        HRESULT result,
        ULONG installerErrorCode = 0,
        bool rebootRequired = false,
        std::wstring errorText = {}) noexcept
    {
        if (errorText.empty() && FAILED(result))
        {
            try
            {
                errorText = ErrorMessage(result);
            }
            catch (...)
            {
                // Preserve the structured result when diagnostic text cannot be allocated.
            }
        }
        std::scoped_lock lock(JobsLock);
        job->State = SUCCEEDED(result) ?
                         ZP_SOFTWARE_DEPLOYMENT_STATE_COMPLETED :
                         ZP_SOFTWARE_DEPLOYMENT_STATE_FAILED;
        job->Progress = SUCCEEDED(result) ? 10000 : job->Progress;
        job->ErrorCode = static_cast<ULONG>(result);
        job->InstallerErrorCode = installerErrorCode;
        job->RebootRequired = rebootRequired;
        job->ErrorText = std::move(errorText);
    }

    winrt::Windows::Foundation::Uri FileUri(const std::wstring& path)
    {
        std::vector<WCHAR> uri(path.size() * 3 + 16);
        DWORD length = static_cast<DWORD>(uri.size());
        winrt::check_hresult(UrlCreateFromPathW(path.c_str(), uri.data(), &length, 0));
        return winrt::Windows::Foundation::Uri(uri.data());
    }

    void RequireElevation()
    {
        TOKEN_ELEVATION elevation;
        DWORD length;

        if (!GetTokenInformation(GetCurrentProcessToken(),
                                 TokenElevation,
                                 &elevation,
                                 sizeof(elevation),
                                 &length))
        {
            winrt::throw_last_error();
        }
        // Fail silently instead of allowing an installer to request interactive elevation.
        if (!elevation.TokenIsElevated)
        {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED));
        }
    }

    void RunMsi(const std::shared_ptr<DeploymentJob>& job)
    {
        WCHAR systemDirectory[MAX_PATH];
        DWORD length = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
        if (length == 0) winrt::throw_last_error();
        if (length >= ARRAYSIZE(systemDirectory) - ARRAYSIZE(L"\\msiexec.exe"))
        {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
        }
        wcscat_s(systemDirectory, L"\\msiexec.exe");
        std::wstring command = L"msiexec.exe /i \"" + job->Payload[0] + L"\" /qn /norestart";
        STARTUPINFOW startup = { sizeof(startup) };
        PROCESS_INFORMATION process;

        RequireElevation();
        UpdateJob(job, ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING, 0);
        if (!CreateProcessW(systemDirectory,
                            command.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &startup,
                            &process))
        {
            winrt::throw_last_error();
        }
        CloseHandle(process.hThread);
        DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode;
        BOOL queried = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        if (!queried)
        {
            winrt::throw_hresult(wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_UNEXPECTED);
        }
        bool reboot = exitCode == ERROR_SUCCESS_REBOOT_INITIATED || exitCode == ERROR_SUCCESS_REBOOT_REQUIRED;
        HRESULT result = exitCode == ERROR_SUCCESS || reboot ? S_OK : HRESULT_FROM_WIN32(exitCode);
        CompleteJob(job, result, exitCode, reboot);
    }

    void RunAppx(const std::shared_ptr<DeploymentJob>& job)
    {
        Appx::PackageManager manager;
        if (job->Action == ZpAdministrationActionUninstall)
        {
            auto operation = manager.RemovePackageAsync(job->Payload[0], Appx::RemovalOptions::None);
            operation.Progress([job](auto const&, Appx::DeploymentProgress progress)
            {
                UpdateJob(job,
                          progress.state == Appx::DeploymentProgressState::Queued ?
                              ZP_SOFTWARE_DEPLOYMENT_STATE_QUEUED :
                              ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING,
                          progress.percentage * 100);
            });
            Appx::DeploymentResult deployment = operation.get();
            HRESULT result = deployment.ExtendedErrorCode();
            CompleteJob(job,
                        result,
                        0,
                        result == HRESULT_FROM_WIN32(ERROR_SUCCESS_REBOOT_REQUIRED),
                        FAILED(result) ? std::wstring(deployment.ErrorText()) : std::wstring());
            return;
        }

        auto package = FileUri(job->Payload[0]);
        winrt::Windows::Foundation::IAsyncOperationWithProgress<Appx::DeploymentResult, Appx::DeploymentProgress>
            operation = nullptr;
        if ((job->Flags & ZP_SOFTWARE_ENGINE_MASK) == ZP_SOFTWARE_ENGINE_APP_INSTALLER)
        {
            operation = manager.AddPackageByAppInstallerFileAsync(package,
                                                                   Appx::AddPackageByAppInstallerOptions::None,
                                                                   nullptr);
        }
        else
        {
            auto dependencies = winrt::single_threaded_vector<winrt::Windows::Foundation::Uri>();
            for (size_t index = 2; index < job->Payload.size(); index++)
            {
                dependencies.Append(FileUri(job->Payload[index]));
            }
            operation = manager.AddPackageAsync(package, dependencies.GetView(), Appx::DeploymentOptions::None);
        }
        operation.Progress([job](auto const&, Appx::DeploymentProgress progress)
        {
            UpdateJob(job,
                      progress.state == Appx::DeploymentProgressState::Queued ?
                          ZP_SOFTWARE_DEPLOYMENT_STATE_QUEUED :
                          ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING,
                      progress.percentage * 100);
        });
        Appx::DeploymentResult deployment = operation.get();
        HRESULT result = deployment.ExtendedErrorCode();
        CompleteJob(job,
                    result,
                    0,
                    result == HRESULT_FROM_WIN32(ERROR_SUCCESS_REBOOT_REQUIRED),
                    FAILED(result) ? std::wstring(deployment.ErrorText()) : std::wstring());
    }

    void ConfigureCatalog(const WinGet::PackageCatalogReference& catalog)
    {
        catalog.AcceptSourceAgreements(true);
        auto authentication = CreateWinGetObject<WinGet::AuthenticationArguments>(WinGetAuthenticationArguments);
        authentication.AuthenticationMode(WinGet::AuthenticationMode::Silent);
        catalog.AuthenticationArguments(authentication);
    }

    WinGet::PackageCatalog ConnectCatalog(const WinGet::PackageCatalogReference& reference)
    {
        WinGet::ConnectResult result = reference.Connect();
        if (result.Status() != WinGet::ConnectResultStatus::Ok)
        {
            HRESULT error = result.ExtendedErrorCode();
            winrt::throw_hresult(FAILED(error) ? error : E_FAIL);
        }
        return result.PackageCatalog();
    }

    WinGet::FindPackagesOptions CreateFindOptions(const std::wstring* identity = nullptr)
    {
        auto options = CreateWinGetObject<WinGet::FindPackagesOptions>(WinGetFindPackagesOptions);
        if (identity != nullptr)
        {
            auto filter = CreateWinGetObject<WinGet::PackageMatchFilter>(WinGetPackageMatchFilter);
            filter.Field(WinGet::PackageMatchField::Id);
            filter.Option(WinGet::PackageFieldMatchOption::EqualsCaseInsensitive);
            filter.Value(*identity);
            options.Filters().Append(filter);
        }
        return options;
    }

    std::vector<WinGet::CatalogPackage> FindPackages(
        const WinGet::PackageCatalog& catalog,
        const std::wstring* identity = nullptr)
    {
        WinGet::FindPackagesResult result = catalog.FindPackages(CreateFindOptions(identity));
        if (result.Status() != WinGet::FindPackagesResultStatus::Ok)
        {
            HRESULT error = result.ExtendedErrorCode();
            winrt::throw_hresult(FAILED(error) ? error : E_FAIL);
        }
        std::vector<WinGet::CatalogPackage> packages;
        packages.reserve(result.Matches().Size());
        for (auto const& match : result.Matches()) packages.push_back(match.CatalogPackage());
        return packages;
    }

    WinGet::PackageCatalog ConnectSource(
        const WinGet::PackageManager& manager,
        ULONG flags,
        bool includeInstalled)
    {
        auto source = manager.GetPredefinedPackageCatalog(
            (flags & ZP_SOFTWARE_SOURCE_MASK) == ZP_SOFTWARE_SOURCE_STORE ?
                WinGet::PredefinedPackageCatalog::MicrosoftStore :
                WinGet::PredefinedPackageCatalog::OpenWindowsCatalog);
        if (!includeInstalled)
        {
            ConfigureCatalog(source);
            return ConnectCatalog(source);
        }
        auto options = CreateWinGetObject<WinGet::CreateCompositePackageCatalogOptions>(WinGetCompositeOptions);
        ConfigureCatalog(source);
        options.Catalogs().Append(source);
        options.CompositeSearchBehavior(WinGet::CompositeSearchBehavior::RemotePackagesFromAllCatalogs);
        return ConnectCatalog(manager.CreateCompositePackageCatalog(options));
    }

    WinGet::PackageCatalog ConnectInstalledPackages(
        const WinGet::PackageManager& manager,
        bool refresh,
        bool trackingOnly)
    {
        auto winget = manager.GetPredefinedPackageCatalog(WinGet::PredefinedPackageCatalog::OpenWindowsCatalog);
        auto store = manager.GetPredefinedPackageCatalog(WinGet::PredefinedPackageCatalog::MicrosoftStore);
        auto options = CreateWinGetObject<WinGet::CreateCompositePackageCatalogOptions>(WinGetCompositeOptions);
        ConfigureCatalog(winget);
        ConfigureCatalog(store);
        if (trackingOnly)
        {
            winget.InstalledPackageInformationOnly(true);
            store.InstalledPackageInformationOnly(true);
        }
        if (!refresh)
        {
            winget.PackageCatalogBackgroundUpdateInterval(winrt::Windows::Foundation::TimeSpan::zero());
            store.PackageCatalogBackgroundUpdateInterval(winrt::Windows::Foundation::TimeSpan::zero());
        }
        options.Catalogs().Append(winget);
        options.Catalogs().Append(store);
        options.CompositeSearchBehavior(WinGet::CompositeSearchBehavior::LocalCatalogs);
        return ConnectCatalog(manager.CreateCompositePackageCatalog(options));
    }

    WinGet::CatalogPackage FindPackage(
        const WinGet::PackageCatalog& catalog,
        const std::wstring& identity)
    {
        auto packages = FindPackages(catalog, &identity);
        auto match = std::find_if(packages.begin(), packages.end(), [&identity](auto const& package)
        {
            return _wcsicmp(package.Id().c_str(), identity.c_str()) == 0;
        });
        if (match == packages.end()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        return *match;
    }

    WinGet::InstallOptions CreateInstallOptions(ULONG flags)
    {
        auto options = CreateWinGetObject<WinGet::InstallOptions>(WinGetInstallOptions);
        options.PackageInstallMode(WinGet::PackageInstallMode::Silent);
        options.PackageInstallScope((flags & ZP_SOFTWARE_SCOPE_MASK) == ZP_SOFTWARE_SCOPE_USER ?
                                        WinGet::PackageInstallScope::User :
                                    (flags & ZP_SOFTWARE_SCOPE_MASK) == ZP_SOFTWARE_SCOPE_MACHINE ?
                                        WinGet::PackageInstallScope::System :
                                        WinGet::PackageInstallScope::Any);
        options.AcceptPackageAgreements(true);
        options.AllowHashMismatch(false);
        return options;
    }

    void TrackInstall(
        const std::shared_ptr<DeploymentJob>& job,
        const winrt::Windows::Foundation::IAsyncOperationWithProgress<WinGet::InstallResult, WinGet::InstallProgress>&
            operation,
        ULONG baseProgress = 0,
        ULONG progressScale = 10000)
    {
        operation.Progress([job, baseProgress, progressScale](auto const&, WinGet::InstallProgress progress)
        {
            ULONG value = static_cast<ULONG>((std::max)(progress.DownloadProgress, progress.InstallationProgress) *
                                             progressScale);
            ULONG state = progress.State == WinGet::PackageInstallProgressState::Downloading ?
                              ZP_SOFTWARE_DEPLOYMENT_STATE_DOWNLOADING :
                          progress.State == WinGet::PackageInstallProgressState::Queued ?
                              ZP_SOFTWARE_DEPLOYMENT_STATE_QUEUED :
                              ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING;
            UpdateJob(job, state, baseProgress + value);
        });
    }

    bool ApplyInstallResult(
        const std::shared_ptr<DeploymentJob>& job,
        const WinGet::InstallResult& result,
        bool complete)
    {
        HRESULT error = result.ExtendedErrorCode();
        if (result.Status() != WinGet::InstallResultStatus::Ok && SUCCEEDED(error)) error = E_FAIL;
        if (complete || FAILED(error))
        {
            CompleteJob(job, error, result.InstallerErrorCode(), result.RebootRequired());
        }
        else if (result.RebootRequired())
        {
            std::scoped_lock lock(JobsLock);
            job->RebootRequired = true;
        }
        return SUCCEEDED(error);
    }

    void RunWinGet(const std::shared_ptr<DeploymentJob>& job)
    {
        RequireElevation();
        auto manager = CreateWinGetObject<WinGet::PackageManager>(WinGetPackageManager);
        UpdateJob(job, ZP_SOFTWARE_DEPLOYMENT_STATE_RESOLVING, 0);
        if ((job->Flags & ZP_SOFTWARE_FLAG_ALL) != 0)
        {
            auto catalog = ConnectInstalledPackages(manager, true, false);
            auto packages = FindPackages(catalog);
            packages.erase(std::remove_if(packages.begin(), packages.end(), [](auto const& package)
            {
                return !package.IsUpdateAvailable();
            }), packages.end());
            if (packages.empty())
            {
                CompleteJob(job, S_OK);
                return;
            }
            for (size_t index = 0; index < packages.size(); index++)
            {
                auto operation = manager.UpgradePackageAsync(packages[index], CreateInstallOptions(job->Flags));
                ULONG base = static_cast<ULONG>(index * 10000 / packages.size());
                ULONG end = static_cast<ULONG>((index + 1) * 10000 / packages.size());
                TrackInstall(job, operation, base, end - base);
                if (!ApplyInstallResult(job, operation.get(), index + 1 == packages.size())) return;
            }
            return;
        }

        if (job->Action == ZpAdministrationActionUninstall)
        {
            auto catalog = ConnectInstalledPackages(manager, false, false);
            auto package = FindPackage(catalog, job->Payload[0]);
            auto options = CreateWinGetObject<WinGet::UninstallOptions>(WinGetUninstallOptions);
            options.PackageUninstallMode(WinGet::PackageUninstallMode::Silent);
            auto operation = manager.UninstallPackageAsync(package, options);
            operation.Progress([job](auto const&, WinGet::UninstallProgress progress)
            {
                UpdateJob(job,
                          progress.State == WinGet::PackageUninstallProgressState::Queued ?
                              ZP_SOFTWARE_DEPLOYMENT_STATE_QUEUED :
                              ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING,
                          static_cast<ULONG>(progress.UninstallationProgress * 10000));
            });
            WinGet::UninstallResult result = operation.get();
            HRESULT error = result.ExtendedErrorCode();
            if (result.Status() != WinGet::UninstallResultStatus::Ok && SUCCEEDED(error)) error = E_FAIL;
            CompleteJob(job, error, result.UninstallerErrorCode(), result.RebootRequired());
            return;
        }

        auto catalog = ConnectSource(manager, job->Flags, job->Action == ZpAdministrationActionUpgrade);
        auto package = FindPackage(catalog, job->Payload[0]);
        auto options = CreateInstallOptions(job->Flags);
        auto operation = job->Action == ZpAdministrationActionUpgrade ?
                             manager.UpgradePackageAsync(package, options) :
                             manager.InstallPackageAsync(package, options);
        TrackInstall(job, operation);
        ApplyInstallResult(job, operation.get(), true);
    }

    void CALLBACK RunDeployment(PTP_CALLBACK_INSTANCE, PVOID context) noexcept
    {
        std::unique_ptr<std::shared_ptr<DeploymentJob>> holder(
            static_cast<std::shared_ptr<DeploymentJob>*>(context));
        auto job = *holder;
        try
        {
            ULONG engine = job->Flags & ZP_SOFTWARE_ENGINE_MASK;
            if (engine == ZP_SOFTWARE_ENGINE_MSI)
            {
                RunMsi(job);
            }
            else if (engine == ZP_SOFTWARE_ENGINE_APPX || engine == ZP_SOFTWARE_ENGINE_APP_INSTALLER)
            {
                Microsoft::WRL::Wrappers::RoInitializeWrapper apartment(RO_INIT_MULTITHREADED);
                winrt::check_hresult(static_cast<HRESULT>(apartment));
                RunAppx(job);
            }
            else if (engine == ZP_SOFTWARE_ENGINE_WINGET)
            {
                Microsoft::WRL::Wrappers::RoInitializeWrapper apartment(RO_INIT_MULTITHREADED);
                winrt::check_hresult(static_cast<HRESULT>(apartment));
                RunWinGet(job);
            }
            else
            {
                std::wstring errorText;
                UpdateJob(job, ZP_SOFTWARE_DEPLOYMENT_STATE_INSTALLING, 0);
                HRESULT result = RunExternalPackageCommand(job, &errorText);
                bool reboot = engine == ZP_SOFTWARE_ENGINE_CHOCOLATEY &&
                              (result == HRESULT_FROM_WIN32(ERROR_SUCCESS_REBOOT_INITIATED) ||
                               result == HRESULT_FROM_WIN32(ERROR_SUCCESS_REBOOT_REQUIRED));
                CompleteJob(job, reboot ? S_OK : result, HRESULT_CODE(result), reboot, std::move(errorText));
            }
        }
        catch (const winrt::hresult_error& error)
        {
            CompleteJob(job, error.code());
        }
        catch (const std::bad_alloc&)
        {
            CompleteJob(job, E_OUTOFMEMORY);
        }
        catch (...)
        {
            CompleteJob(job, E_FAIL);
        }
    }

    std::vector<std::wstring> DecodePayload(PCWCH payload, ULONG payloadLength)
    {
        std::vector<std::wstring> values;
        ULONG offset = 0;

        while (offset < payloadLength)
        {
            ULONG end = offset;
            while (end < payloadLength && payload[end] != UNICODE_NULL) end++;
            if (end == offset || end == payloadLength || end - offset > MaximumPayloadValueLength ||
                values.size() == MaximumPayloadValues)
            {
                winrt::throw_hresult(E_INVALIDARG);
            }
            values.emplace_back(payload + offset, end - offset);
            offset = end + 1;
        }
        return values;
    }

    void ValidateDeployment(
        ZP_ADMINISTRATION_ACTION action,
        ULONG flags,
        const std::vector<std::wstring>& payload)
    {
        constexpr ULONG ValidFlags = ZP_SOFTWARE_ENGINE_MASK | ZP_SOFTWARE_SOURCE_MASK |
                                     ZP_SOFTWARE_SCOPE_MASK | ZP_SOFTWARE_FLAG_ALL;
        ULONG engine = flags & ZP_SOFTWARE_ENGINE_MASK;

        if ((flags & ~ValidFlags) != 0 || payload.size() < 2 || payload[1].size() > 260 ||
            engine < ZP_SOFTWARE_ENGINE_MSI || engine > ZP_SOFTWARE_ENGINE_DOTNET_TOOL)
        {
            winrt::throw_hresult(E_INVALIDARG);
        }
        if (engine == ZP_SOFTWARE_ENGINE_MSI)
        {
            if (action != ZpAdministrationActionInstall || flags != engine || payload.size() != 2)
                winrt::throw_hresult(E_INVALIDARG);
        }
        else if (engine == ZP_SOFTWARE_ENGINE_APP_INSTALLER)
        {
            if (action != ZpAdministrationActionInstall || flags != engine || payload.size() != 2)
                winrt::throw_hresult(E_INVALIDARG);
        }
        else if (engine == ZP_SOFTWARE_ENGINE_APPX)
        {
            if ((action != ZpAdministrationActionInstall && action != ZpAdministrationActionUninstall) ||
                flags != engine || (action == ZpAdministrationActionUninstall && payload.size() != 2) ||
                payload.size() > 34)
            {
                winrt::throw_hresult(E_INVALIDARG);
            }
        }
        else if (engine == ZP_SOFTWARE_ENGINE_WINGET)
        {
            ULONG source = flags & ZP_SOFTWARE_SOURCE_MASK;
            ULONG scope = flags & ZP_SOFTWARE_SCOPE_MASK;
            bool all = (flags & ZP_SOFTWARE_FLAG_ALL) != 0;
            bool validFlags = all ? flags == (engine | ZP_SOFTWARE_FLAG_ALL) :
                              action == ZpAdministrationActionUninstall ? flags == engine :
                              source != 0 && source != ZP_SOFTWARE_SOURCE_MASK &&
                                  scope != ZP_SOFTWARE_SCOPE_MASK &&
                                  (source != ZP_SOFTWARE_SOURCE_STORE || scope == 0);

            if ((action != ZpAdministrationActionInstall && action != ZpAdministrationActionUninstall &&
                  action != ZpAdministrationActionUpgrade) ||
                !validFlags || (all && action != ZpAdministrationActionUpgrade) ||
                payload.size() != 2)
            {
                winrt::throw_hresult(E_INVALIDARG);
            }
        }
        else
        {
            ULONG scope = flags & ZP_SOFTWARE_SCOPE_MASK;
            bool all = FlagOn(flags, ZP_SOFTWARE_FLAG_ALL);
            bool allSupported = engine == ZP_SOFTWARE_ENGINE_NPM || engine == ZP_SOFTWARE_ENGINE_CHOCOLATEY;
            bool scopeValid = scope == 0 ||
                              (engine == ZP_SOFTWARE_ENGINE_PIP && scope == ZP_SOFTWARE_SCOPE_USER &&
                               action != ZpAdministrationActionUninstall);

            if ((action != ZpAdministrationActionInstall && action != ZpAdministrationActionUninstall &&
                 action != ZpAdministrationActionUpgrade) ||
                (flags & ZP_SOFTWARE_SOURCE_MASK) != 0 || !scopeValid ||
                (all && (action != ZpAdministrationActionUpgrade || !allSupported)) ||
                payload.size() > 3 || (action == ZpAdministrationActionUninstall && payload.size() != 2) ||
                (all && (payload.size() != 2 || payload[0] != L"*")))
            {
                winrt::throw_hresult(E_INVALIDARG);
            }
        }
    }

    Json::IJsonValue ParseJson(const std::wstring& text)
    {
        return Json::JsonValue::Parse(text);
    }

    void EnumeratePipPackages(
        ZP_SOFTWARE_PACKAGE_CALLBACK callback,
        PVOID context)
    {
        std::wstring python = FindPython();
        if (python.empty()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        std::wstring output = RunProcessText(python,
                                             { L"-m", L"pip", L"--disable-pip-version-check", L"--no-input",
                                               L"list", L"--format=json" });
        for (auto const& value : ParseJson(output).GetArray())
        {
            Json::JsonObject package = value.GetObject();
            std::wstring name(package.GetNamedString(L"name"));
            std::wstring version(package.GetNamedString(L"version"));
            ZP_SOFTWARE_PACKAGE_INFO info = {
                ZP_SOFTWARE_ENGINE_PIP, name.c_str(), name.c_str(), version.c_str(), L""
            };
            if (!callback(&info, context)) winrt::throw_hresult(E_OUTOFMEMORY);
        }
    }

    void EnumerateNpmPackages(
        ZP_SOFTWARE_PACKAGE_CALLBACK callback,
        PVOID context)
    {
        std::wstring node = FindPathExecutable(L"node.exe");
        std::wstring npm = FindNpmCli(node);
        if (node.empty() || npm.empty()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        ULONG exitCode;
        std::wstring output = RunProcessText(node,
                                             { npm, L"ls", L"--global", L"--depth=0", L"--json",
                                               L"--loglevel=error" },
                                             false,
                                             PackageEnumerationTimeout,
                                             PackageEnumerationOutputLimit,
                                             &exitCode);
        // npm reports dependency problems with a nonzero exit code while still returning a usable JSON inventory.
        Json::JsonObject root = ParseJson(output).GetObject();
        Json::JsonObject dependencies = root.GetNamedObject(L"dependencies", Json::JsonObject());
        for (auto const& entry : dependencies)
        {
            std::wstring name(entry.Key());
            std::wstring version(entry.Value().GetObject().GetNamedString(L"version", L""));
            if (version.empty()) continue;
            ZP_SOFTWARE_PACKAGE_INFO info = {
                ZP_SOFTWARE_ENGINE_NPM, name.c_str(), name.c_str(), version.c_str(), L""
            };
            if (!callback(&info, context)) winrt::throw_hresult(E_OUTOFMEMORY);
        }
    }

    void EnumerateChocolateyPackages(
        ZP_SOFTWARE_PACKAGE_CALLBACK callback,
        PVOID context)
    {
        std::wstring chocolatey = FindPathExecutable(L"choco.exe");
        if (chocolatey.empty()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        std::wstring output = RunProcessText(chocolatey, { L"list", L"--limit-output", L"--no-progress" });
        size_t offset = 0;
        while (offset < output.size())
        {
            size_t end = output.find(L'\n', offset);
            std::wstring_view line(output.data() + offset,
                                   (end == std::wstring::npos ? output.size() : end) - offset);
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            size_t separator = line.find(L'|');
            if (separator != std::wstring_view::npos && separator != 0 && separator + 1 < line.size())
            {
                std::wstring name(line.substr(0, separator));
                std::wstring version(line.substr(separator + 1));
                ZP_SOFTWARE_PACKAGE_INFO info = {
                    ZP_SOFTWARE_ENGINE_CHOCOLATEY, name.c_str(), name.c_str(), version.c_str(), L""
                };
                if (!callback(&info, context)) winrt::throw_hresult(E_OUTOFMEMORY);
            }
            if (end == std::wstring::npos) break;
            offset = end + 1;
        }
    }

    void EnumerateDotNetToolPackages(
        ZP_SOFTWARE_PACKAGE_CALLBACK callback,
        PVOID context)
    {
        std::wstring dotnet = FindPathExecutable(L"dotnet.exe");
        if (dotnet.empty()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        std::wstring output = RunProcessText(dotnet, { L"tool", L"list", L"--global", L"--format", L"json" });
        Json::JsonObject root = ParseJson(output).GetObject();
        for (auto const& value : root.GetNamedArray(L"data", Json::JsonArray()))
        {
            Json::JsonObject package = value.GetObject();
            std::wstring name(package.GetNamedString(L"packageId"));
            std::wstring version(package.GetNamedString(L"version"));
            ZP_SOFTWARE_PACKAGE_INFO info = {
                ZP_SOFTWARE_ENGINE_DOTNET_TOOL, name.c_str(), name.c_str(), version.c_str(), L""
            };
            if (!callback(&info, context)) winrt::throw_hresult(E_OUTOFMEMORY);
        }
    }

    bool IsPackageNameCharacter(WCHAR value)
    {
        return (value >= L'0' && value <= L'9') || (value >= L'A' && value <= L'Z') ||
               (value >= L'a' && value <= L'z') || value == L'.' || value == L'_' || value == L'-';
    }

    bool IsPackageNamePart(std::wstring_view value)
    {
        return !value.empty() && std::all_of(value.begin(), value.end(), IsPackageNameCharacter);
    }

    bool IsPackageNameValid(const std::wstring& value, bool npmName)
    {
        if (value.empty() || value.front() == L'-') return false;
        size_t separator = value.find(L'/');
        if (!npmName || value.front() != L'@')
        {
            return separator == std::wstring::npos && IsPackageNamePart(value);
        }
        return separator > 1 && separator + 1 < value.size() && value.find(L'/', separator + 1) == std::wstring::npos &&
               IsPackageNamePart(std::wstring_view(value).substr(1, separator - 1)) &&
               IsPackageNamePart(std::wstring_view(value).substr(separator + 1));
    }

    bool IsPackageVersionValid(const std::wstring& value, bool allowEpoch)
    {
        return !value.empty() && value.front() != L'-' &&
               std::all_of(value.begin(), value.end(), [allowEpoch](WCHAR character)
               {
                   return IsPackageNameCharacter(character) || character == L'+' ||
                          (allowEpoch && character == L'!');
               });
    }

    HRESULT RunExternalPackageCommand(const std::shared_ptr<DeploymentJob>& job, std::wstring* errorText)
    {
        ULONG engine = job->Flags & ZP_SOFTWARE_ENGINE_MASK;
        bool all = FlagOn(job->Flags, ZP_SOFTWARE_FLAG_ALL);
        const std::wstring& package = job->Payload[0];
        const std::wstring version = job->Payload.size() == 3 ? job->Payload[2] : std::wstring();
        if ((!all && !IsPackageNameValid(package, engine == ZP_SOFTWARE_ENGINE_NPM)) ||
            (!version.empty() && !IsPackageVersionValid(version, engine == ZP_SOFTWARE_ENGINE_PIP)))
        {
            return E_INVALIDARG;
        }
        std::wstring application;
        std::vector<std::wstring> arguments;
        if (engine == ZP_SOFTWARE_ENGINE_PIP)
        {
            application = FindPython();
            arguments = { L"-m", L"pip", L"--disable-pip-version-check", L"--no-input" };
            if (all) return E_NOTIMPL;
            if (job->Action == ZpAdministrationActionUninstall)
            {
                arguments.insert(arguments.end(), { L"uninstall", L"--yes", package });
            }
            else
            {
                arguments.push_back(L"install");
                if (job->Action == ZpAdministrationActionUpgrade) arguments.push_back(L"--upgrade");
                if (FlagOn(job->Flags, ZP_SOFTWARE_SCOPE_USER)) arguments.push_back(L"--user");
                arguments.push_back(version.empty() ? package : package + L"==" + version);
            }
        }
        else if (engine == ZP_SOFTWARE_ENGINE_NPM)
        {
            application = FindPathExecutable(L"node.exe");
            std::wstring npm = FindNpmCli(application);
            if (npm.empty()) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            arguments = { npm,
                          job->Action == ZpAdministrationActionInstall ? L"install" :
                          job->Action == ZpAdministrationActionUninstall ? L"uninstall" : L"update",
                          L"--global", L"--no-audit", L"--no-fund", L"--loglevel=error" };
            if (!all) arguments.push_back(version.empty() ? package : package + L"@" + version);
        }
        else if (engine == ZP_SOFTWARE_ENGINE_CHOCOLATEY)
        {
            RequireElevation();
            application = FindPathExecutable(L"choco.exe");
            arguments = { job->Action == ZpAdministrationActionInstall ? L"install" :
                          job->Action == ZpAdministrationActionUninstall ? L"uninstall" : L"upgrade",
                          all ? L"all" : package,
                          L"--yes", L"--no-progress", L"--limit-output" };
            if (!version.empty()) arguments.push_back(L"--version=" + version);
        }
        else if (engine == ZP_SOFTWARE_ENGINE_DOTNET_TOOL)
        {
            if (all) return E_NOTIMPL;
            application = FindPathExecutable(L"dotnet.exe");
            arguments = { L"tool",
                          job->Action == ZpAdministrationActionInstall ? L"install" :
                          job->Action == ZpAdministrationActionUninstall ? L"uninstall" : L"update",
                          L"--global", package };
            if (!version.empty()) arguments.insert(arguments.end(), { L"--version", version });
        }
        else
        {
            return E_INVALIDARG;
        }
        if (application.empty()) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        ULONG exitCode;
        std::wstring output = RunProcessText(application,
                                             arguments,
                                             true,
                                             PackageOperationTimeout,
                                             PackageOperationOutputLimit,
                                             &exitCode);
        if (exitCode == ERROR_SUCCESS) return S_OK;
        *errorText = std::move(output);
        return HRESULT_FROM_WIN32(exitCode);
    }

    template<typename Operation>
    HRESULT Invoke(Operation&& operation) noexcept
    {
        try
        {
            return operation();
        }
        catch (const winrt::hresult_error& error)
        {
            return error.code();
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            return E_FAIL;
        }
    }
}

HRESULT
ZpSoftware_EnumeratePackageProviders(
    _In_ ZP_SOFTWARE_PACKAGE_PROVIDER_CALLBACK callback,
    _In_opt_ PVOID context)
{
    return Invoke([&]() -> HRESULT
    {
        if (callback == nullptr) return E_INVALIDARG;
        std::wstring account = CurrentAccountName();
        ZP_SOFTWARE_PACKAGE_PROVIDER_INFO accountInfo = { 0, 0, L"context", account.c_str(), L"", L"" };
        if (!callback(&accountInfo, context)) return E_OUTOFMEMORY;
        constexpr ULONG CommonCapabilities = ZP_SOFTWARE_PACKAGE_CAPABILITY_INSTALL |
                                             ZP_SOFTWARE_PACKAGE_CAPABILITY_UPGRADE |
                                             ZP_SOFTWARE_PACKAGE_CAPABILITY_UNINSTALL;
        auto emit = [&](ULONG provider,
                        ULONG capabilities,
                        PCWSTR identity,
                        PCWSTR name,
                        const std::wstring& runtime,
                        const std::wstring& manager) -> HRESULT
        {
            ZP_SOFTWARE_PACKAGE_PROVIDER_INFO info = {
                provider, capabilities, identity, name, runtime.c_str(), manager.c_str()
            };
            return callback(&info, context) ? S_OK : E_OUTOFMEMORY;
        };
        try
        {
            Microsoft::WRL::Wrappers::RoInitializeWrapper apartment(RO_INIT_MULTITHREADED);
            winrt::check_hresult(static_cast<HRESULT>(apartment));
            CreateWinGetObject<WinGet::PackageManager>(WinGetPackageManager);
            HRESULT result = emit(ZP_SOFTWARE_ENGINE_WINGET,
                                  CommonCapabilities | ZP_SOFTWARE_PACKAGE_CAPABILITY_UPGRADE_ALL,
                                  L"winget",
                                  L"WinGet",
                                  L"Windows",
                                  L"");
            if (FAILED(result)) return result;
        }
        catch (const winrt::hresult_error&)
        {
        }
        std::wstring python = FindPython();
        if (!python.empty())
        {
            try
            {
                std::wstring runtime = RunProcessText(python, { L"--version" });
                std::wstring manager = RunProcessText(python,
                                                      { L"-m", L"pip", L"--disable-pip-version-check",
                                                        L"--version" });
                HRESULT result = emit(ZP_SOFTWARE_ENGINE_PIP,
                                      CommonCapabilities,
                                      L"pip",
                                      L"Python",
                                      runtime,
                                      manager);
                if (FAILED(result)) return result;
            }
            catch (const winrt::hresult_error&)
            {
            }
        }
        std::wstring node = FindPathExecutable(L"node.exe");
        std::wstring npm = FindNpmCli(node);
        if (!node.empty() && !npm.empty())
        {
            try
            {
                std::wstring runtime = RunProcessText(node, { L"--version" });
                std::wstring manager = RunProcessText(node, { npm, L"--version" });
                HRESULT result = emit(ZP_SOFTWARE_ENGINE_NPM,
                                      CommonCapabilities | ZP_SOFTWARE_PACKAGE_CAPABILITY_UPGRADE_ALL,
                                      L"npm",
                                      L"Node.js",
                                      runtime,
                                      manager);
                if (FAILED(result)) return result;
            }
            catch (const winrt::hresult_error&)
            {
            }
        }
        std::wstring chocolatey = FindPathExecutable(L"choco.exe");
        if (!chocolatey.empty())
        {
            try
            {
                std::wstring manager = RunProcessText(chocolatey, { L"--version" });
                HRESULT result = emit(ZP_SOFTWARE_ENGINE_CHOCOLATEY,
                                      CommonCapabilities | ZP_SOFTWARE_PACKAGE_CAPABILITY_UPGRADE_ALL,
                                      L"chocolatey",
                                      L"Chocolatey",
                                      L"Windows",
                                      manager);
                if (FAILED(result)) return result;
            }
            catch (const winrt::hresult_error&)
            {
            }
        }
        std::wstring dotnet = FindPathExecutable(L"dotnet.exe");
        if (!dotnet.empty())
        {
            try
            {
                std::wstring runtime = RunProcessText(dotnet, { L"--version" });
                HRESULT result = emit(ZP_SOFTWARE_ENGINE_DOTNET_TOOL,
                                      CommonCapabilities,
                                      L"dotnet",
                                      L".NET",
                                      runtime,
                                      L"dotnet tool");
                if (FAILED(result)) return result;
            }
            catch (const winrt::hresult_error&)
            {
            }
        }
        return S_OK;
    });
}

HRESULT
ZpSoftware_EnumeratePackages(
    _In_ PCWSTR provider,
    _In_ ZP_SOFTWARE_PACKAGE_CALLBACK callback,
    _In_opt_ PVOID context)
{
    return Invoke([&]() -> HRESULT
    {
        if (provider == nullptr || callback == nullptr) return E_INVALIDARG;
        if (_wcsicmp(provider, L"pip") == 0)
        {
            EnumeratePipPackages(callback, context);
            return S_OK;
        }
        if (_wcsicmp(provider, L"npm") == 0)
        {
            EnumerateNpmPackages(callback, context);
            return S_OK;
        }
        if (_wcsicmp(provider, L"chocolatey") == 0)
        {
            EnumerateChocolateyPackages(callback, context);
            return S_OK;
        }
        if (_wcsicmp(provider, L"dotnet") == 0)
        {
            EnumerateDotNetToolPackages(callback, context);
            return S_OK;
        }
        if (_wcsicmp(provider, L"winget") != 0) return E_INVALIDARG;
        Microsoft::WRL::Wrappers::RoInitializeWrapper apartment(RO_INIT_MULTITHREADED);
        winrt::check_hresult(static_cast<HRESULT>(apartment));
        auto manager = CreateWinGetObject<WinGet::PackageManager>(WinGetPackageManager);
        auto catalog = ConnectInstalledPackages(manager, false, true);
        for (auto const& package : FindPackages(catalog))
        {
            WinGet::PackageVersionInfo installed = package.InstalledVersion();
            WinGet::PackageVersionInfo available = package.DefaultInstallVersion();
            if (!installed || !available) continue;
            std::wstring identity(package.Id());
            std::wstring name(package.Name());
            std::wstring version(installed.Version());
            std::wstring source(available.PackageCatalog().Info().Name());
            ZP_SOFTWARE_PACKAGE_INFO info = {
                ZP_SOFTWARE_ENGINE_WINGET,
                identity.c_str(),
                name.c_str(),
                version.c_str(),
                source.c_str()
            };
            if (!callback(&info, context)) return E_OUTOFMEMORY;
        }
        return S_OK;
    });
}

HRESULT
ZpSoftware_StartDeployment(
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_ ULONG Flags,
    _In_ PCWSTR Id,
    _In_reads_(PayloadLength) PCWCH Payload,
    _In_ ULONG PayloadLength)
{
    return Invoke([&]() -> HRESULT
    {
        GUID parsedId;
        if (Id == nullptr || wcslen(Id) != 36 || FAILED(CLSIDFromString(Id, &parsedId)) || Payload == nullptr ||
            PayloadLength == 0)
        {
            return E_INVALIDARG;
        }
        auto values = DecodePayload(Payload, PayloadLength);
        ValidateDeployment(Action, Flags, values);
        auto job = std::make_shared<DeploymentJob>(Id, std::move(values), Action, Flags);

        {
            std::scoped_lock lock(JobsLock);
            if (std::any_of(Jobs.begin(), Jobs.end(), [](auto const& existing)
            {
                return existing->State < ZP_SOFTWARE_DEPLOYMENT_STATE_COMPLETED;
            }))
            {
                return HRESULT_FROM_WIN32(ERROR_BUSY);
            }
            if (std::any_of(Jobs.begin(), Jobs.end(), [&job](auto const& existing)
            {
                return _wcsicmp(existing->Id.c_str(), job->Id.c_str()) == 0;
            }))
            {
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            }
            if (Jobs.size() == MaximumJobs)
            {
                auto completed = std::find_if(Jobs.begin(), Jobs.end(), [](auto const& existing)
                {
                    return existing->State >= ZP_SOFTWARE_DEPLOYMENT_STATE_COMPLETED;
                });
                if (completed == Jobs.end()) return HRESULT_FROM_WIN32(ERROR_BUSY);
                Jobs.erase(completed);
            }
            Jobs.push_back(job);
        }

        auto context = new (std::nothrow) std::shared_ptr<DeploymentJob>(job);
        HRESULT result;
        if (context == nullptr)
        {
            result = E_OUTOFMEMORY;
        }
        else if (TrySubmitThreadpoolCallback(RunDeployment, context, nullptr))
        {
            return S_OK;
        }
        else
        {
            result = HRESULT_FROM_WIN32(GetLastError());
            delete context;
        }
        std::scoped_lock lock(JobsLock);
        Jobs.pop_back();
        return result;
    });
}

HRESULT
ZpSoftware_EnumerateDeployments(
    _In_ ZP_SOFTWARE_DEPLOYMENT_CALLBACK callback,
    _In_opt_ PVOID context)
{
    return Invoke([&]() -> HRESULT
    {
        if (callback == nullptr) return E_INVALIDARG;
        std::scoped_lock lock(JobsLock);
        for (auto const& job : Jobs)
        {
            ZP_SOFTWARE_DEPLOYMENT_INFO info = {
                job->Id.c_str(),
                job->Payload[1].c_str(),
                job->Payload[0].c_str(),
                job->ErrorText.c_str(),
                job->State | job->Progress << ZP_SOFTWARE_DEPLOYMENT_PROGRESS_SHIFT,
                job->Flags | static_cast<ULONG>(job->Action) << ZP_SOFTWARE_DEPLOYMENT_ACTION_SHIFT |
                    (job->RebootRequired ? ZP_SOFTWARE_DEPLOYMENT_FLAG_REBOOT_REQUIRED : 0),
                static_cast<ULONGLONG>(job->ErrorCode) |
                    static_cast<ULONGLONG>(job->InstallerErrorCode) << 32
            };
            if (!callback(&info, context)) return E_OUTOFMEMORY;
        }
        return S_OK;
    });
}
