#include "autostart_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"
#include "app_identity_bi.h"

#include <taskschd.h>
#include <shellapi.h>
#include <string>

const char *autostart_bi::ARG_INSTALL_TASK = "--install-task";
const char *autostart_bi::ARG_REMOVE_TASK = "--remove-task";
const char *autostart_bi::ARG_FROM_TASK = "--from-task";

namespace
{
    const wchar_t *TASK_NAME = APP_TASK_NAME;
    const char *RUN_KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *RUN_VALUE = APP_RUN_VALUE;

    const CLSID CLSID_TaskScheduler_bi =
        {0x0f87369f, 0xa4e5, 0x4cfc, {0xbd, 0x3e, 0x73, 0xe6, 0x15, 0x45, 0x72, 0xdd}};
    const IID IID_ITaskService_bi =
        {0x2faba4c7, 0x4da9, 0x4013, {0x96, 0x97, 0x20, 0xcc, 0x3f, 0xd4, 0x0f, 0x85}};
    const IID IID_ILogonTrigger_bi =
        {0x72dade38, 0xfae4, 0x4b3e, {0xba, 0xf4, 0x5d, 0x00, 0x9a, 0xf0, 0x2b, 0x1c}};
    const IID IID_IExecAction_bi =
        {0x4c3d624d, 0xfd6b, 0x49a3, {0xb9, 0xb7, 0x09, 0xcb, 0x3c, 0xd3, 0xf0, 0x47}};

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

    bool createTask()
    {
        const std::string &exe = paths_bi::exePath();
        if (exe.empty())
        {
            log_bi::write("autostart: exe path unknown, cannot create task");
            return false;
        }

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
        if (SUCCEEDED(task->get_Principal(&principal)) && principal)
        {
            BSTR id = SysAllocString(L"Author");
            principal->put_Id(id);
            SysFreeString(id);

            principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
            principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
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
        if (SUCCEEDED(task->get_Triggers(&triggers)) && triggers)
        {
            ITrigger *trigger = NULL;
            if (SUCCEEDED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) && trigger)
            {
                ILogonTrigger *logon = NULL;
                if (SUCCEEDED(trigger->QueryInterface(IID_ILogonTrigger_bi, (void **)&logon)) && logon)
                {
                    BSTR id = SysAllocString(L"LogonTrigger");
                    logon->put_Id(id);
                    SysFreeString(id);

                    BSTR delay = SysAllocString(L"PT10S");
                    logon->put_Delay(delay);
                    SysFreeString(delay);

                    logon->Release();
                }
                trigger->Release();
            }
            triggers->Release();
        }

        IActionCollection *actions = NULL;
        if (SUCCEEDED(task->get_Actions(&actions)) && actions)
        {
            IAction *action = NULL;
            if (SUCCEEDED(actions->Create(TASK_ACTION_EXEC, &action)) && action)
            {
                IExecAction *exec = NULL;
                if (SUCCEEDED(action->QueryInterface(IID_IExecAction_bi, (void **)&exec)) && exec)
                {
                    std::wstring wexe = widen(exe);
                    BSTR path = SysAllocString(wexe.c_str());
                    exec->put_Path(path);
                    SysFreeString(path);

                    BSTR args = SysAllocString(L"--from-task");
                    exec->put_Arguments(args);
                    SysFreeString(args);

                    size_t slash = wexe.find_last_of(L'\\');
                    if (slash != std::wstring::npos)
                    {
                        BSTR dir = SysAllocString(wexe.substr(0, slash).c_str());
                        exec->put_WorkingDirectory(dir);
                        SysFreeString(dir);
                    }

                    exec->Release();
                }
                action->Release();
            }
            actions->Release();
        }

        VARIANT empty;
        VariantInit(&empty);

        BSTR name = SysAllocString(TASK_NAME);
        IRegisteredTask *registered = NULL;

        hr = folder->RegisterTaskDefinition(
            name, task, TASK_CREATE_OR_UPDATE,
            empty, empty, TASK_LOGON_INTERACTIVE_TOKEN,
            empty, &registered);

        SysFreeString(name);

        bool ok = SUCCEEDED(hr);
        if (ok)
            log_bi::write("autostart: scheduled task created (elevated, logon trigger)");
        else
            log_bi::writeErr((unsigned long)hr, "autostart: RegisterTaskDefinition failed");

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
        const std::string &exe = paths_bi::exePath();
        if (exe.empty())
            return false;

        SHELLEXECUTEINFOA sei;
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = "runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = arg;
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExA(&sei))
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
            WaitForSingleObject(sei.hProcess, 30000);
            GetExitCodeProcess(sei.hProcess, &code);
            CloseHandle(sei.hProcess);
        }

        return code == 0;
    }

    bool runKeyExists()
    {
        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;

        LONG r = RegQueryValueExA(key, RUN_VALUE, NULL, NULL, NULL, NULL);
        RegCloseKey(key);
        return r == ERROR_SUCCESS;
    }

    bool writeRunKey()
    {
        const std::string &exe = paths_bi::exePath();
        if (exe.empty())
            return false;

        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
            return false;

        std::string quoted = "\"" + exe + "\"";

        LONG r = RegSetValueExA(key, RUN_VALUE, 0, REG_SZ,
                                (const BYTE *)quoted.c_str(),
                                (DWORD)quoted.size() + 1);
        RegCloseKey(key);

        if (r != ERROR_SUCCESS)
            log_bi::writeErr((unsigned long)r, "autostart: writing HKCU Run value failed");

        return r == ERROR_SUCCESS;
    }

    bool deleteRunKey()
    {
        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
            return false;

        LONG r = RegDeleteValueA(key, RUN_VALUE);
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

autostart_bi::mode_bi autostart_bi::current()
{
    if (taskExists())
        return AUTOSTART_ADMIN;
    if (runKeyExists())
        return AUTOSTART_NORMAL;
    return AUTOSTART_OFF;
}

bool autostart_bi::setMode(mode_bi mode)
{
    if (mode != AUTOSTART_NORMAL)
        deleteRunKey();

    if (mode != AUTOSTART_ADMIN && taskExists())
    {
        if (isElevated())
            deleteTask();
        else
            elevateSelfFor(ARG_REMOVE_TASK);
    }

    switch (mode)
    {
    case AUTOSTART_OFF:
        return true;

    case AUTOSTART_NORMAL:
        return writeRunKey();

    case AUTOSTART_ADMIN:
        if (isElevated())
            return createTask();
        return elevateSelfFor(ARG_INSTALL_TASK);
    }

    return false;
}

bool autostart_bi::runTask()
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
    if (!cmdLine)
        return false;

    std::string args(cmdLine);

    if (args.find(ARG_INSTALL_TASK) != std::string::npos)
    {
        *exitCode = createTask() ? 0 : 1;
        return true;
    }

    if (args.find(ARG_REMOVE_TASK) != std::string::npos)
    {
        *exitCode = deleteTask() ? 0 : 1;
        return true;
    }

    return false;
}
