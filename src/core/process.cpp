#include "process.h"

#include "util.h"

#include <atomic>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pmp3 {

static const size_t kStderrKeep = 64 * 1024;

#ifdef _WIN32

std::string quoteWindowsArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        } else if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back('"');
    return out;
}

static std::wstring buildCommandLine(const std::vector<std::string>& args) {
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(' ');
        cmd += quoteWindowsArg(args[i]);
    }
    return widen(cmd);
}

static std::string lastErrorText(const char* what) {
    DWORD code = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string text = std::string(what) + ": " + (msg ? trim(narrow(msg)) : std::string("error ")) +
                       " (" + std::to_string(code) + ")";
    if (msg) LocalFree(msg);
    return text;
}

struct Process::Impl {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE outRead = nullptr;
    HANDLE errRead = nullptr;
    std::thread errThread;
    std::mutex errMutex;
    std::string errText;
    std::string buffer;
    bool eof = false;
    std::atomic<bool> killed{false};

    ~Impl() {
        if (errThread.joinable()) errThread.join();
        if (outRead) CloseHandle(outRead);
        if (errRead) CloseHandle(errRead);
        if (process) CloseHandle(process);
        if (job) CloseHandle(job);
    }
};

Process::Process() : impl_(new Impl) {}
Process::~Process() {
    if (impl_->process) {
        DWORD code = 0;
        if (GetExitCodeProcess(impl_->process, &code) && code == STILL_ACTIVE) kill();
    }
}

bool Process::start(const std::vector<std::string>& args, std::string& err) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outWrite = nullptr, errWrite = nullptr;
    if (!CreatePipe(&impl_->outRead, &outWrite, &sa, 0) || !CreatePipe(&impl_->errRead, &errWrite, &sa, 0)) {
        err = lastErrorText("CreatePipe");
        return false;
    }
    SetHandleInformation(impl_->outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // Restrict inheritance to exactly the three handles we want, so the
    // child never sees e.g. the native-messaging pipes of our own process.
    HANDLE inherit[3] = {nul, outWrite, errWrite};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto attrList = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf.data();
    InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr,
                              nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = outWrite;
    si.StartupInfo.hStdError = errWrite;
    si.lpAttributeList = attrList;

    // A job object lets kill() take down the whole tree.
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (impl_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }

    std::wstring cmd = buildCommandLine(args);
    std::wstring app = widen(args[0]);
    // A bare program name ("yt-dlp") is searched on PATH by CreateProcess
    // only when no application name is given.
    const wchar_t* appName = toPath(args[0]).is_absolute() ? app.c_str() : nullptr;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(appName, &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT |
                                 CREATE_UNICODE_ENVIRONMENT,
                             nullptr, nullptr, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrList);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        err = lastErrorText(("Could not start " + args[0]).c_str());
        return false;
    }
    if (impl_->job) AssignProcessToJobObject(impl_->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    impl_->process = pi.hProcess;

    Impl* impl = impl_.get();
    impl_->errThread = std::thread([impl] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(impl->errRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
            std::lock_guard<std::mutex> lock(impl->errMutex);
            impl->errText.append(buf, n);
            if (impl->errText.size() > kStderrKeep) impl->errText.erase(0, impl->errText.size() - kStderrKeep);
        }
    });
    return true;
}

bool Process::readLine(std::string& line) {
    for (;;) {
        size_t pos = impl_->buffer.find_first_of("\r\n");
        if (pos != std::string::npos) {
            line = impl_->buffer.substr(0, pos);
            size_t skip = pos + 1;
            if (impl_->buffer[pos] == '\r' && skip < impl_->buffer.size() && impl_->buffer[skip] == '\n') ++skip;
            impl_->buffer.erase(0, skip);
            return true;
        }
        if (impl_->eof) {
            if (impl_->buffer.empty()) return false;
            line.swap(impl_->buffer);
            impl_->buffer.clear();
            return true;
        }
        char buf[4096];
        DWORD n = 0;
        if (!ReadFile(impl_->outRead, buf, sizeof(buf), &n, nullptr) || n == 0)
            impl_->eof = true;
        else
            impl_->buffer.append(buf, n);
    }
}

int Process::wait() {
    std::string dummy;
    while (readLine(dummy)) {
    }
    if (!impl_->process) return -1;
    WaitForSingleObject(impl_->process, INFINITE);
    if (impl_->errThread.joinable()) impl_->errThread.join();
    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    return impl_->killed ? -2 : (int)code;
}

void Process::kill() {
    impl_->killed = true;
    if (impl_->job) TerminateJobObject(impl_->job, 1);
    if (impl_->process) TerminateProcess(impl_->process, 1);
}

std::string Process::stderrText() {
    std::lock_guard<std::mutex> lock(impl_->errMutex);
    return impl_->errText;
}

bool spawnDetached(const std::vector<std::string>& args, std::string& err, bool hideWindow) {
    std::wstring cmd = buildCommandLine(args);
    std::wstring app = widen(args[0]);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hideWindow) {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT | (hideWindow ? CREATE_NO_WINDOW : DETACHED_PROCESS);
    // The browser may place its native-messaging host in a job that is
    // killed when the host exits; try to break away so downloads survive.
    BOOL ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB,
                             nullptr, nullptr, &si, &pi);
    if (!ok) ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        err = lastErrorText(("Could not start " + args[0]).c_str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

#else  // POSIX

struct Process::Impl {
    pid_t pid = -1;
    int outFd = -1;
    int errFd = -1;
    std::thread errThread;
    std::mutex errMutex;
    std::string errText;
    std::string buffer;
    bool eof = false;
    bool waited = false;
    int exitCode = -1;
    std::atomic<bool> killed{false};

    ~Impl() {
        if (errThread.joinable()) errThread.join();
        if (outFd >= 0) close(outFd);
        if (errFd >= 0) close(errFd);
    }
};

Process::Process() : impl_(new Impl) {}
Process::~Process() {
    if (impl_->pid > 0 && !impl_->waited) {
        kill();
        int status = 0;
        waitpid(impl_->pid, &status, 0);
        impl_->waited = true;
    }
}

bool Process::start(const std::vector<std::string>& args, std::string& err) {
    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        err = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        err = std::string("fork: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        setpgid(0, 0);  // own process group so kill() reaches grandchildren
        int nul = open("/dev/null", O_RDONLY);
        dup2(nul, 0);
        dup2(outPipe[1], 1);
        dup2(errPipe[1], 2);
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        execvp(argv[0], argv.data());
        std::fprintf(stderr, "Could not start %s: %s\n", argv[0], std::strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);
    close(outPipe[1]);
    close(errPipe[1]);
    impl_->pid = pid;
    impl_->outFd = outPipe[0];
    impl_->errFd = errPipe[0];
    Impl* impl = impl_.get();
    impl_->errThread = std::thread([impl] {
        char buf[4096];
        ssize_t n;
        while ((n = read(impl->errFd, buf, sizeof(buf))) > 0) {
            std::lock_guard<std::mutex> lock(impl->errMutex);
            impl->errText.append(buf, (size_t)n);
            if (impl->errText.size() > kStderrKeep) impl->errText.erase(0, impl->errText.size() - kStderrKeep);
        }
    });
    return true;
}

bool Process::readLine(std::string& line) {
    for (;;) {
        size_t pos = impl_->buffer.find_first_of("\r\n");
        if (pos != std::string::npos) {
            line = impl_->buffer.substr(0, pos);
            size_t skip = pos + 1;
            if (impl_->buffer[pos] == '\r' && skip < impl_->buffer.size() && impl_->buffer[skip] == '\n') ++skip;
            impl_->buffer.erase(0, skip);
            return true;
        }
        if (impl_->eof) {
            if (impl_->buffer.empty()) return false;
            line.swap(impl_->buffer);
            impl_->buffer.clear();
            return true;
        }
        char buf[4096];
        ssize_t n = read(impl_->outFd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0)
            impl_->eof = true;
        else
            impl_->buffer.append(buf, (size_t)n);
    }
}

int Process::wait() {
    std::string dummy;
    while (readLine(dummy)) {
    }
    if (impl_->pid <= 0) return -1;
    if (!impl_->waited) {
        int status = 0;
        while (waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
        }
        impl_->waited = true;
        impl_->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    if (impl_->errThread.joinable()) impl_->errThread.join();
    return impl_->killed ? -2 : impl_->exitCode;
}

void Process::kill() {
    impl_->killed = true;
    if (impl_->pid > 0 && !impl_->waited) ::kill(-impl_->pid, SIGKILL);
}

std::string Process::stderrText() {
    std::lock_guard<std::mutex> lock(impl_->errMutex);
    return impl_->errText;
}

bool spawnDetached(const std::vector<std::string>& args, std::string& err, bool) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        err = std::string("fork: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, 0);
        dup2(nul, 1);
        dup2(nul, 2);
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
}

#endif

int runCapture(const std::vector<std::string>& args, std::string& out, std::string& err) {
    Process p;
    if (!p.start(args, err)) return -1;
    std::string line;
    out.clear();
    while (p.readLine(line)) {
        out += line;
        out += '\n';
    }
    int code = p.wait();
    err = p.stderrText();
    return code;
}

}  // namespace pmp3
