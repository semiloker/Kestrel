#include "autostart_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"
#include "app_identity_bi.h"

#include <aclapi.h>
#include <sddl.h>
#include <taskschd.h>
#include <shellapi.h>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

const char *autostart_bi::ARG_INSTALL_TASK = "--install-task";
const char *autostart_bi::ARG_REMOVE_TASK = "--remove-task";
const char *autostart_bi::ARG_FROM_TASK = "--from-task";
const char *autostart_bi::ARG_AUTOSTART = "--autostart";

namespace
{
    const wchar_t *TASK_NAME = APP_TASK_NAME;
    const wchar_t *RUN_KEY =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t *RUN_VALUE = L"Kestrel";

    const CLSID CLSID_TaskScheduler_bi =
        {0x0f87369f, 0xa4e5, 0x4cfc, {0xbd, 0x3e, 0x73, 0xe6, 0x15, 0x45, 0x72, 0xdd}};
    const IID IID_ITaskService_bi =
        {0x2faba4c7, 0x4da9, 0x4013, {0x96, 0x97, 0x20, 0xcc, 0x3f, 0xd4, 0x0f, 0x85}};
    const IID IID_ILogonTrigger_bi =
        {0x72dade38, 0xfae4, 0x4b3e, {0xba, 0xf4, 0x5d, 0x00, 0x9a, 0xf0, 0x2b, 0x1c}};
    const IID IID_IExecAction_bi =
        {0x4c3d624d, 0xfd6b, 0x49a3, {0xb9, 0xb7, 0x09, 0xcb, 0x3c, 0xd3, 0xf0, 0x47}};

    bool finalExePath(std::wstring *pathOut)
    {
        std::wstring path = paths_bi::exePathWide();
        if (path.empty())
            return false;

        HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD needed = GetFinalPathNameByHandleW(file, NULL, 0,
                                                 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (needed == 0)
        {
            CloseHandle(file);
            return false;
        }

        std::vector<wchar_t> buffer((size_t)needed + 1);
        DWORD length = GetFinalPathNameByHandleW(file, &buffer[0], (DWORD)buffer.size(),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(file);

        if (length == 0 || length >= buffer.size())
            return false;

        path.assign(&buffer[0], length);

        const std::wstring extendedPrefix = L"\\\\?\\";
        const std::wstring uncPrefix = L"\\\\?\\UNC\\";

        if (path.compare(0, uncPrefix.size(), uncPrefix) == 0)
            return false;  // Never elevate an executable from a network share.

        if (path.compare(0, extendedPrefix.size(), extendedPrefix) == 0)
            path.erase(0, extendedPrefix.size());

        if (path.size() < 3 || path[1] != L':' || path[2] != L'\\')
            return false;

        wchar_t root[] = {path[0], L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            return false;

        *pathOut = path;
        return true;
    }

    bool mediumIntegrityToken(HANDLE *tokenOut)
    {
        *tokenOut = NULL;

        HANDLE processToken = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &processToken))
            return false;

        TOKEN_ELEVATION elevation = {};
        DWORD returned = 0;
        if (!GetTokenInformation(processToken, TokenElevation, &elevation,
                                 sizeof(elevation), &returned))
        {
            CloseHandle(processToken);
            return false;
        }

        HANDLE sourceToken = processToken;
        HANDLE linkedToken = NULL;

        if (elevation.TokenIsElevated)
        {
            TOKEN_LINKED_TOKEN linked = {};
            if (!GetTokenInformation(processToken, TokenLinkedToken, &linked,
                                     sizeof(linked), &returned))
            {
                CloseHandle(processToken);
                return false;
            }

            linkedToken = linked.LinkedToken;
            sourceToken = linkedToken;
        }

        HANDLE impersonation = NULL;
        BOOL duplicated = DuplicateTokenEx(sourceToken, TOKEN_QUERY | TOKEN_IMPERSONATE,
                                           NULL, SecurityImpersonation,
                                           TokenImpersonation, &impersonation);

        if (linkedToken)
            CloseHandle(linkedToken);
        CloseHandle(processToken);

        if (!duplicated)
            return false;

        std::vector<BYTE> integrityBuffer(256);
        if (!GetTokenInformation(impersonation, TokenIntegrityLevel,
                                 &integrityBuffer[0], (DWORD)integrityBuffer.size(),
                                 &returned))
        {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                CloseHandle(impersonation);
                return false;
            }

            if (returned < sizeof(TOKEN_MANDATORY_LABEL))
            {
                CloseHandle(impersonation);
                return false;
            }

            integrityBuffer.resize(returned);
            if (!GetTokenInformation(impersonation, TokenIntegrityLevel,
                                     &integrityBuffer[0], (DWORD)integrityBuffer.size(),
                                     &returned))
            {
                CloseHandle(impersonation);
                return false;
            }
        }
        else if (returned < sizeof(TOKEN_MANDATORY_LABEL))
        {
            CloseHandle(impersonation);
            return false;
        }

        TOKEN_MANDATORY_LABEL *label =
            reinterpret_cast<TOKEN_MANDATORY_LABEL *>(&integrityBuffer[0]);
        if (!label->Label.Sid || !IsValidSid(label->Label.Sid))
        {
            CloseHandle(impersonation);
            return false;
        }

        UCHAR subAuthorities = *GetSidSubAuthorityCount(label->Label.Sid);
        if (subAuthorities == 0)
        {
            CloseHandle(impersonation);
            return false;
        }

        DWORD integrityRid =
            *GetSidSubAuthority(label->Label.Sid, subAuthorities - 1);

        if (integrityRid >= SECURITY_MANDATORY_HIGH_RID)
        {
            CloseHandle(impersonation);
            return false;
        }

        *tokenOut = impersonation;
        return true;
    }

    bool descriptorAccess(PSECURITY_DESCRIPTOR descriptor, HANDLE token,
                          DWORD *accessOut)
    {
        if (!descriptor || !token || !accessOut ||
            !IsValidSecurityDescriptor(descriptor))
        {
            return false;
        }

        *accessOut = 0;

        GENERIC_MAPPING mapping = {
            FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};

        std::vector<BYTE> privileges(1024);
        DWORD privilegeSize = (DWORD)privileges.size();
        DWORD granted = 0;
        BOOL accessStatus = FALSE;

        BOOL checked = AccessCheck(
            descriptor, token, MAXIMUM_ALLOWED, &mapping,
            reinterpret_cast<PRIVILEGE_SET *>(&privileges[0]),
            &privilegeSize, &granted, &accessStatus);

        if (!checked && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            privileges.resize(privilegeSize);
            checked = AccessCheck(
                descriptor, token, MAXIMUM_ALLOWED, &mapping,
                reinterpret_cast<PRIVILEGE_SET *>(&privileges[0]),
                &privilegeSize, &granted, &accessStatus);
        }

        if (!checked || !accessStatus)
            return false;

        *accessOut = granted;
        return true;
    }

    bool effectiveAccess(const std::wstring &path, HANDLE token, DWORD *accessOut)
    {
        PSECURITY_DESCRIPTOR descriptor = NULL;
        DWORD error = GetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION,
            NULL, NULL, NULL, NULL, &descriptor);
        if (error != ERROR_SUCCESS || !descriptor)
        {
            log_bi::writeErr(error, "autostart: cannot inspect executable path security");
            return false;
        }

        bool ok = descriptorAccess(descriptor, token, accessOut);
        LocalFree(descriptor);
        return ok;
    }

    bool protectedExecutablePath(std::wstring *pathOut)
    {
        std::wstring path;
        if (!finalExePath(&path))
        {
            log_bi::write("autostart: elevated mode refused because the executable "
                          "path cannot be resolved to a fixed local drive");
            return false;
        }

        HANDLE token = NULL;
        if (!mediumIntegrityToken(&token))
        {
            log_bi::write("autostart: elevated mode refused because a standard-user "
                          "access token is unavailable");
            return false;
        }

        const DWORD fileModification =
            FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
            FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER;

        DWORD rights = 0;
        bool safe = effectiveAccess(path, token, &rights) &&
                    (rights & fileModification) == 0;

        size_t slash = path.find_last_of(L'\\');
        std::wstring directory =
            (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
        bool immediateParent = true;

        while (safe && !directory.empty())
        {
            DWORD directoryModification =
                DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD;

            // The containing directory must not permit creating or modifying
            // a replacement executable. Higher ancestors only need protection
            // against deleting/replacing an existing path component.
            if (immediateParent)
            {
                directoryModification |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
                                         FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
            }

            safe = effectiveAccess(directory, token, &rights) &&
                   (rights & directoryModification) == 0;

            if (directory.size() == 3 && directory[1] == L':' &&
                directory[2] == L'\\')
            {
                break;
            }

            slash = directory.find_last_of(L'\\');
            if (slash == std::wstring::npos)
                break;

            directory = (slash == 2) ? directory.substr(0, 3)
                                     : directory.substr(0, slash);
            immediateParent = false;
        }

        CloseHandle(token);

        if (!safe)
        {
            log_bi::write("autostart: elevated mode refused because the executable "
                          "or its directory is writable by the standard user; "
                          "move Kestrel to Program Files or another "
                          "administrator-protected directory");
            return false;
        }

        *pathOut = path;
        return true;
    }

    bool hasExactCommandArgument(const wchar_t *expected,
                                 bool requireSingleArgument)
    {
        int argumentCount = 0;
        LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(),
                                               &argumentCount);
        if (!arguments)
            return false;

        bool found = false;
        if (!requireSingleArgument || argumentCount == 2)
        {
            for (int i = 1; i < argumentCount; ++i)
            {
                if (wcscmp(arguments[i], expected) == 0)
                {
                    found = true;
                    break;
                }
            }
        }

        LocalFree(arguments);
        return found;
    }

    std::wstring widen(const std::string &s)
    {
        if (s.empty())
            return std::wstring();

        int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
        if (n <= 0)
            return std::wstring();

        std::wstring out((size_t)n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], n);
        return out;
    }

    bool protectedTaskSddl(std::wstring *sddlOut)
    {
        HANDLE token = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        {
            CloseHandle(token);
            return false;
        }

        std::vector<BYTE> buffer(bytes);
        if (!GetTokenInformation(token, TokenUser, &buffer[0], bytes, &bytes))
        {
            CloseHandle(token);
            return false;
        }
        CloseHandle(token);

        TOKEN_USER *user = reinterpret_cast<TOKEN_USER *>(&buffer[0]);
        if (!user->User.Sid || !IsValidSid(user->User.Sid))
            return false;

        LPWSTR sidText = NULL;
        if (!ConvertSidToStringSidW(user->User.Sid, &sidText) || !sidText)
            return false;

        // The principal may read and run the task, but only SYSTEM or an
        // elevated administrator may change/delete it. Keeping Administrators
        // as owner prevents the unelevated user from gaining implicit
        // WRITE_DAC as the task owner.
        std::wstring sddl =
            L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;";
        sddl += sidText;
        sddl += L")";
        LocalFree(sidText);

        *sddlOut = sddl;
        return true;
    }

    struct com_scope_bi
    {
        bool needUninit = false;

        com_scope_bi()
        {
            HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            needUninit = SUCCEEDED(hr);
        }

        ~com_scope_bi()
        {
            if (needUninit)
                CoUninitialize();
        }
    };

    bool connectScheduler(ITaskService **serviceOut, ITaskFolder **folderOut)
    {
        *serviceOut = NULL;
        *folderOut = NULL;

        ITaskService *service = NULL;
        HRESULT hr = CoCreateInstance(CLSID_TaskScheduler_bi, NULL, CLSCTX_INPROC_SERVER,
                                      IID_ITaskService_bi, (void **)&service);
        if (FAILED(hr))
        {
            log_bi::writeErr((unsigned long)hr, "autostart: CoCreateInstance(TaskScheduler) failed");
            return false;
        }

        VARIANT empty;
        VariantInit(&empty);

        hr = service->Connect(empty, empty, empty, empty);
        if (FAILED(hr))
        {
            log_bi::writeErr((unsigned long)hr, "autostart: ITaskService::Connect failed");
            service->Release();
            return false;
        }

        BSTR root = SysAllocString(L"\\");
        ITaskFolder *folder = NULL;
        hr = service->GetFolder(root, &folder);
        SysFreeString(root);

        if (FAILED(hr))
        {
            log_bi::writeErr((unsigned long)hr, "autostart: GetFolder failed");
            service->Release();
            return false;
        }

        *serviceOut = service;
        *folderOut = folder;
        return true;
    }

    bool registeredTaskProtected(IRegisteredTask *task)
    {
        BSTR sddl = NULL;
        HRESULT hr = task->GetSecurityDescriptor(
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION,
            &sddl);
        if (FAILED(hr) || !sddl)
            return false;

        PSECURITY_DESCRIPTOR descriptor = NULL;
        bool converted = ConvertStringSecurityDescriptorToSecurityDescriptorW(
                             sddl, SDDL_REVISION_1, &descriptor, NULL) != FALSE;
        SysFreeString(sddl);
        if (!converted || !descriptor)
        {
            if (descriptor)
                LocalFree(descriptor);
            return false;
        }

        HANDLE token = NULL;
        DWORD rights = 0;
        bool checked = mediumIntegrityToken(&token) &&
                       descriptorAccess(descriptor, token, &rights);

        if (token)
            CloseHandle(token);
        LocalFree(descriptor);

        const DWORD taskModification =
            FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
            FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER;
        return checked && (rights & taskModification) == 0;
    }

    bool createTask()
    {
        std::wstring wexe;
        if (!protectedExecutablePath(&wexe))
            return false;

        com_scope_bi com;

        ITaskService *service = NULL;
        ITaskFolder *folder = NULL;
        if (!connectScheduler(&service, &folder))
            return false;

        ITaskDefinition *task = NULL;
        HRESULT hr = service->NewTask(0, &task);
        if (FAILED(hr))
        {
            log_bi::writeErr((unsigned long)hr, "autostart: NewTask failed");
            folder->Release();
            service->Release();
            return false;
        }

        bool definitionValid = true;

        IRegistrationInfo *regInfo = NULL;
        if (SUCCEEDED(task->get_RegistrationInfo(&regInfo)) && regInfo)
        {
            BSTR author = SysAllocString(L"Kestrel");
            BSTR descr = SysAllocString(L"Starts Kestrel at logon with elevated rights "
                                        L"(required for ETW frame metrics).");
            regInfo->put_Author(author);
            regInfo->put_Description(descr);
            SysFreeString(author);
            SysFreeString(descr);
            regInfo->Release();
        }

        IPrincipal *principal = NULL;
        if (FAILED(task->get_Principal(&principal)) || !principal)
        {
            definitionValid = false;
        }
        else
        {
            BSTR id = SysAllocString(L"Author");
            HRESULT idResult = principal->put_Id(id);
            SysFreeString(id);

            HRESULT logonResult =
                principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
            HRESULT levelResult =
                principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
            if (FAILED(idResult) || FAILED(logonResult) || FAILED(levelResult))
                definitionValid = false;

            principal->Release();
        }

        ITaskSettings *settings = NULL;
        if (SUCCEEDED(task->get_Settings(&settings)) && settings)
        {
            settings->put_Enabled(VARIANT_TRUE);
            settings->put_StartWhenAvailable(VARIANT_TRUE);
            settings->put_AllowDemandStart(VARIANT_TRUE);
            settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);

            settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);

            BSTR noLimit = SysAllocString(L"PT0S");
            settings->put_ExecutionTimeLimit(noLimit);
            SysFreeString(noLimit);

            IIdleSettings *idle = NULL;
            if (SUCCEEDED(settings->get_IdleSettings(&idle)) && idle)
            {
                idle->put_StopOnIdleEnd(VARIANT_FALSE);
                idle->Release();
            }

            settings->Release();
        }

        ITriggerCollection *triggers = NULL;
        if (FAILED(task->get_Triggers(&triggers)) || !triggers)
        {
            definitionValid = false;
        }
        else
        {
            ITrigger *trigger = NULL;
            if (FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) || !trigger)
            {
                definitionValid = false;
            }
            else
            {
                ILogonTrigger *logon = NULL;
                if (FAILED(trigger->QueryInterface(
                        IID_ILogonTrigger_bi, (void **)&logon)) ||
                    !logon)
                {
                    definitionValid = false;
                }
                else
                {
                    BSTR id = SysAllocString(L"LogonTrigger");
                    HRESULT idResult = logon->put_Id(id);
                    SysFreeString(id);

                    BSTR delay = SysAllocString(L"PT10S");
                    HRESULT delayResult = logon->put_Delay(delay);
                    SysFreeString(delay);

                    if (FAILED(idResult) || FAILED(delayResult))
                        definitionValid = false;

                    logon->Release();
                }
                trigger->Release();
            }
            triggers->Release();
        }

        IActionCollection *actions = NULL;
        if (FAILED(task->get_Actions(&actions)) || !actions)
        {
            definitionValid = false;
        }
        else
        {
            IAction *action = NULL;
            if (FAILED(actions->Create(TASK_ACTION_EXEC, &action)) || !action)
            {
                definitionValid = false;
            }
            else
            {
                IExecAction *exec = NULL;
                if (FAILED(action->QueryInterface(
                        IID_IExecAction_bi, (void **)&exec)) ||
                    !exec)
                {
                    definitionValid = false;
                }
                else
                {
                    BSTR path = SysAllocString(wexe.c_str());
                    HRESULT pathResult = exec->put_Path(path);
                    SysFreeString(path);

                    BSTR args = SysAllocString(L"--from-task");
                    HRESULT argsResult = exec->put_Arguments(args);
                    SysFreeString(args);

                    HRESULT directoryResult = S_OK;
                    size_t slash = wexe.find_last_of(L'\\');
                    if (slash != std::wstring::npos)
                    {
                        BSTR dir = SysAllocString(wexe.substr(0, slash).c_str());
                        directoryResult = exec->put_WorkingDirectory(dir);
                        SysFreeString(dir);
                    }

                    if (FAILED(pathResult) || FAILED(argsResult) ||
                        FAILED(directoryResult))
                    {
                        definitionValid = false;
                    }

                    exec->Release();
                }
                action->Release();
            }
            actions->Release();
        }

        VARIANT empty;
        VariantInit(&empty);

        if (!definitionValid)
        {
            log_bi::write("autostart: task definition could not be secured");
            task->Release();
            folder->Release();
            service->Release();
            return false;
        }

        std::wstring taskSddl;
        if (!protectedTaskSddl(&taskSddl))
        {
            log_bi::write("autostart: cannot build a protected task DACL");
            task->Release();
            folder->Release();
            service->Release();
            return false;
        }

        BSTR name = SysAllocString(TASK_NAME);
        BSTR securityText = SysAllocString(taskSddl.c_str());
        if (!name || !securityText)
        {
            SysFreeString(securityText);
            SysFreeString(name);
            task->Release();
            folder->Release();
            service->Release();
            return false;
        }

        VARIANT security;
        VariantInit(&security);
        security.vt = VT_BSTR;
        security.bstrVal = securityText;

        IRegisteredTask *registered = NULL;

        hr = folder->RegisterTaskDefinition(
            name, task,
            TASK_CREATE_OR_UPDATE | TASK_DONT_ADD_PRINCIPAL_ACE,
            empty, empty, TASK_LOGON_INTERACTIVE_TOKEN,
            security, &registered);

        VariantClear(&security);

        bool registeredOk = SUCCEEDED(hr) && registered;
        bool ok = registeredOk && registeredTaskProtected(registered);
        if (registeredOk && !ok)
        {
            log_bi::write("autostart: task DACL verification failed; removing task");
            HRESULT deleteResult = folder->DeleteTask(name, 0);
            if (FAILED(deleteResult))
            {
                log_bi::writeErr((unsigned long)deleteResult,
                                 "autostart: cannot remove task with unsafe DACL");
            }
        }

        SysFreeString(name);

        if (ok)
        {
            log_bi::write("autostart: protected scheduled task created "
                          "(elevated, logon trigger)");
        }
        else if (!registeredOk)
        {
            log_bi::writeErr((unsigned long)hr, "autostart: RegisterTaskDefinition failed");
        }

        if (registered)
            registered->Release();
        task->Release();
        folder->Release();
        service->Release();

        return ok;
    }

    bool deleteTask()
    {
        com_scope_bi com;

        ITaskService *service = NULL;
        ITaskFolder *folder = NULL;
        if (!connectScheduler(&service, &folder))
            return false;

        BSTR name = SysAllocString(TASK_NAME);
        HRESULT hr = folder->DeleteTask(name, 0);
        SysFreeString(name);

        folder->Release();
        service->Release();

        bool ok = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (ok)
            log_bi::write("autostart: scheduled task removed");
        else
            log_bi::writeErr((unsigned long)hr, "autostart: DeleteTask failed");

        return ok;
    }

    bool elevateSelfFor(const char *arg)
    {
        std::wstring executable;
        std::wstring parameters;

        if (strcmp(arg, autostart_bi::ARG_REMOVE_TASK) == 0)
        {
            std::vector<wchar_t> systemDirectory(MAX_PATH);
            UINT length = GetSystemDirectoryW(&systemDirectory[0],
                                              (UINT)systemDirectory.size());
            if (length == 0 || length >= systemDirectory.size())
                return false;

            executable.assign(&systemDirectory[0], length);
            executable += L"\\schtasks.exe";
            parameters = L"/Delete /TN \"";
            parameters += TASK_NAME;
            parameters += L"\" /F";
        }
        else
        {
            if (!protectedExecutablePath(&executable))
                return false;
            parameters = widen(arg);
        }

        SHELLEXECUTEINFOW sei;
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = executable.c_str();
        sei.lpParameters = parameters.c_str();
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExW(&sei))
        {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED)
                log_bi::write("autostart: user declined the UAC prompt");
            else
                log_bi::writeErr(err, "autostart: ShellExecuteEx(runas) failed");
            return false;
        }

        DWORD code = 1;
        if (sei.hProcess)
        {
            DWORD wait = WaitForSingleObject(sei.hProcess, 30000);
            if (wait == WAIT_OBJECT_0)
                GetExitCodeProcess(sei.hProcess, &code);
            CloseHandle(sei.hProcess);
        }

        return code == 0;
    }

    bool runKeyExists()
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0,
                          KEY_READ, &key) != ERROR_SUCCESS)
            return false;

        LONG r = RegQueryValueExW(key, RUN_VALUE, NULL, NULL, NULL, NULL);
        RegCloseKey(key);
        return r == ERROR_SUCCESS;
    }

    bool writeRunKey()
    {
        const std::wstring &exe = paths_bi::exePathWide();
        if (exe.empty())
            return false;

        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                            NULL, &key, NULL) != ERROR_SUCCESS)
            return false;

        std::wstring quoted = L"\"" + exe + L"\" --autostart";

        LONG r = RegSetValueExW(
            key, RUN_VALUE, 0, REG_SZ,
            reinterpret_cast<const BYTE *>(quoted.c_str()),
            (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);

        if (r != ERROR_SUCCESS)
            log_bi::writeErr((unsigned long)r, "autostart: writing HKCU Run value failed");

        return r == ERROR_SUCCESS;
    }

    bool deleteRunKey()
    {
        HKEY key;
        LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0,
                                    KEY_WRITE, &key);
        if (opened == ERROR_FILE_NOT_FOUND)
            return true;
        if (opened != ERROR_SUCCESS)
            return false;

        LONG r = RegDeleteValueW(key, RUN_VALUE);
        RegCloseKey(key);

        return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }
}

bool autostart_bi::isElevated()
{
    bool elevated = false;
    HANDLE token = NULL;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION el;
        DWORD size = sizeof(el);
        if (GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size))
            elevated = (el.TokenIsElevated != 0);
        CloseHandle(token);
    }

    return elevated;
}

bool autostart_bi::executablePathProtected()
{
    std::wstring path;
    return protectedExecutablePath(&path);
}

bool autostart_bi::taskExists()
{
    com_scope_bi com;

    ITaskService *service = NULL;
    ITaskFolder *folder = NULL;
    if (!connectScheduler(&service, &folder))
        return false;

    BSTR name = SysAllocString(TASK_NAME);
    IRegisteredTask *task = NULL;
    HRESULT hr = folder->GetTask(name, &task);
    SysFreeString(name);

    if (task)
        task->Release();
    folder->Release();
    service->Release();

    return SUCCEEDED(hr);
}

bool autostart_bi::taskPointsToThisExe()
{
    std::wstring wexe;
    if (!protectedExecutablePath(&wexe))
        return false;

    com_scope_bi com;

    ITaskService *service = NULL;
    ITaskFolder *folder = NULL;
    if (!connectScheduler(&service, &folder))
        return false;

    BSTR name = SysAllocString(TASK_NAME);
    IRegisteredTask *task = NULL;
    folder->GetTask(name, &task);
    SysFreeString(name);

    bool match = false;
    bool taskProtected = task && registeredTaskProtected(task);
    if (taskProtected)
    {
        ITaskDefinition *def = NULL;
        if (SUCCEEDED(task->get_Definition(&def)) && def)
        {
            bool principalMatches = false;
            IPrincipal *principal = NULL;
            if (SUCCEEDED(def->get_Principal(&principal)) && principal)
            {
                TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_LUA;
                TASK_LOGON_TYPE logonType = TASK_LOGON_NONE;
                principalMatches =
                    SUCCEEDED(principal->get_RunLevel(&level)) &&
                    SUCCEEDED(principal->get_LogonType(&logonType)) &&
                    level == TASK_RUNLEVEL_HIGHEST &&
                    logonType == TASK_LOGON_INTERACTIVE_TOKEN;
                principal->Release();
            }

            IActionCollection *actions = NULL;
            if (SUCCEEDED(def->get_Actions(&actions)) && actions)
            {
                LONG count = 0;
                if (principalMatches &&
                    SUCCEEDED(actions->get_Count(&count)) &&
                    count == 1)
                {
                    IAction *action = NULL;
                    if (SUCCEEDED(actions->get_Item(1, &action)) && action)
                    {
                        IExecAction *exec = NULL;
                        if (SUCCEEDED(action->QueryInterface(
                                IID_IExecAction_bi, (void **)&exec)) &&
                            exec)
                        {
                            BSTR path = NULL;
                            BSTR arguments = NULL;
                            BSTR workingDirectory = NULL;
                            size_t slash = wexe.find_last_of(L'\\');
                            std::wstring expectedDirectory =
                                slash == std::wstring::npos
                                    ? std::wstring()
                                    : wexe.substr(0, slash);
                            if (SUCCEEDED(exec->get_Path(&path)) && path &&
                                SUCCEEDED(exec->get_Arguments(&arguments)) &&
                                arguments &&
                                SUCCEEDED(exec->get_WorkingDirectory(
                                    &workingDirectory)) &&
                                workingDirectory)
                            {
                                match =
                                    _wcsicmp(path, wexe.c_str()) == 0 &&
                                    wcscmp(arguments, L"--from-task") == 0 &&
                                    !expectedDirectory.empty() &&
                                    _wcsicmp(workingDirectory,
                                             expectedDirectory.c_str()) == 0;
                            }
                            SysFreeString(workingDirectory);
                            SysFreeString(arguments);
                            SysFreeString(path);
                            exec->Release();
                        }
                        action->Release();
                    }
                }
                actions->Release();
            }
            def->Release();
        }
    }
    if (task)
        task->Release();

    folder->Release();
    service->Release();
    return match;
}

autostart_bi::mode_bi autostart_bi::current()
{
    if (taskExists() && taskPointsToThisExe())
        return AUTOSTART_ADMIN;
    if (runKeyExists())
        return AUTOSTART_NORMAL;
    return AUTOSTART_OFF;
}

bool autostart_bi::setMode(mode_bi mode)
{
    switch (mode)
    {
    case AUTOSTART_OFF:
    {
        bool runKeyRemoved = deleteRunKey();
        bool taskRemoved = true;

        if (taskExists())
        {
            taskRemoved = isElevated()
                              ? deleteTask()
                              : elevateSelfFor(ARG_REMOVE_TASK);
        }

        return runKeyRemoved && taskRemoved;
    }

    case AUTOSTART_NORMAL:
        if (taskExists())
        {
            bool removed = isElevated()
                               ? deleteTask()
                               : elevateSelfFor(ARG_REMOVE_TASK);
            if (!removed)
                return false;
        }
        return writeRunKey();

    case AUTOSTART_ADMIN:
    {
        std::wstring protectedPath;
        if (!protectedExecutablePath(&protectedPath))
            return false;

        bool created = isElevated()
                           ? createTask()
                           : elevateSelfFor(ARG_INSTALL_TASK);
        if (!created)
            return false;

        return deleteRunKey();
    }
    }

    return false;
}

bool autostart_bi::runTask()
{
    // Never execute a same-name task unless its privilege and sole action
    // still match the definition we create.
    if (!taskPointsToThisExe())
        return false;

    com_scope_bi com;

    ITaskService *service = NULL;
    ITaskFolder *folder = NULL;
    if (!connectScheduler(&service, &folder))
        return false;

    BSTR name = SysAllocString(TASK_NAME);
    IRegisteredTask *task = NULL;
    HRESULT hr = folder->GetTask(name, &task);
    SysFreeString(name);

    bool ok = false;
    if (SUCCEEDED(hr) && task)
    {
        VARIANT empty;
        VariantInit(&empty);

        IRunningTask *running = NULL;
        hr = task->Run(empty, &running);
        ok = SUCCEEDED(hr);

        if (running)
            running->Release();
        if (!ok)
            log_bi::writeErr((unsigned long)hr, "autostart: IRegisteredTask::Run failed");

        task->Release();
    }

    folder->Release();
    service->Release();
    return ok;
}

bool autostart_bi::handleCommandLine(const char *cmdLine, int *exitCode)
{
    if (!cmdLine || !exitCode)
        return false;

    if (hasExactCommandArgument(L"--from-task", false))
    {
        bool exactTaskInvocation =
            hasExactCommandArgument(L"--from-task", true);
        std::wstring protectedPath;
        if (!protectedExecutablePath(&protectedPath))
        {
            log_bi::write("autostart: refusing to run an elevated legacy task "
                          "from an unprotected executable path");
            if (isElevated() && !deleteTask())
            {
                log_bi::write("autostart: unsafe legacy task could not be removed");
            }
            *exitCode = 1;
            return true;
        }

        // Migrate a legacy task whose default DACL lets the unelevated user
        // rewrite its highest-privilege action. A genuine task launch is
        // already elevated, so the repair does not require another prompt.
        if (!taskPointsToThisExe())
        {
            bool elevated = isElevated();
            if (!elevated || !createTask())
            {
                log_bi::write("autostart: refusing an unprotected task definition");
                if (elevated && !deleteTask())
                {
                    log_bi::write("autostart: unprotected task could not be removed");
                }
                *exitCode = 1;
                return true;
            }
        }

        if (!exactTaskInvocation)
        {
            log_bi::write("autostart: refusing extra arguments from an elevated task");
            *exitCode = 1;
            return true;
        }
    }

    if (hasExactCommandArgument(L"--install-task", true))
    {
        *exitCode = createTask() ? 0 : 1;
        return true;
    }

    if (hasExactCommandArgument(L"--remove-task", true))
    {
        *exitCode = deleteTask() ? 0 : 1;
        return true;
    }

    return false;
}
