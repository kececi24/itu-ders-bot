#include "include/console.hpp"
#include "test_helpers.hpp"
#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>
#include <cstdio>
#include <vector>

namespace {
using test_helpers::require;
HANDLE input_handle;
DWORD baseline;

void key(WORD code, wchar_t character = 0, DWORD modifiers = 0, WORD repeat_count = 1) {
    INPUT_RECORD records[2]{};
    for (int i = 0; i < 2; ++i) {
        records[i].EventType = KEY_EVENT;
        records[i].Event.KeyEvent = {i == 0, repeat_count, code, 0, {character}, modifiers};
    }
    DWORD written = 0;
    require(WriteConsoleInputW(input_handle, records, 2, &written) && written == 2, "inject console key");
}

void line(const std::wstring& text) {
    for (wchar_t c : text) key(0, c);
    key(VK_RETURN, L'\r');
}

DWORD mode() {
    DWORD value = 0;
    require(GetConsoleMode(input_handle, &value), "read console mode");
    return value;
}

void restored() {
    require(mode() == baseline, "console input mode not restored");
}

struct PromptProbe : std::stringbuf {
    std::atomic<bool> saw_prompt{false};
    std::atomic<bool> prompt_hidden{false};
    int sync() override {
        if (str().find("PASSWORD-PROMPT") != std::string::npos) {
            prompt_hidden = !(mode() & ENABLE_ECHO_INPUT) && (mode() & ENABLE_LINE_INPUT);
            saw_prompt = true;
        }
        return 0;
    }
};

struct Capture {
    PromptProbe buffer;
    std::streambuf* previous = std::cout.rdbuf(&buffer);
    ~Capture() { std::cout.rdbuf(previous); }
};

void interrupt_case(DWORD signal) {
    FlushConsoleInputBuffer(input_handle);
    itu::platform::ConsoleSession session;
    const DWORD saved = mode();
    Capture capture;
    std::thread trigger([signal, &capture] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!capture.buffer.saw_prompt && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(capture.buffer.saw_prompt, "prompt was not displayed before interrupt");
        BOOL sent = GenerateConsoleCtrlEvent(signal, 0);
        require(sent, "GenerateConsoleCtrlEvent delivery failed");
    });
    bool interrupted = false;
    try {
        std::string value;
        interrupted = !itu::platform::read_line(value, true, "PASSWORD-PROMPT");
    } catch (const std::exception&) {
        interrupted = true;
    }
    trigger.join();
    require(interrupted, "control signal did not interrupt password input");
    require(mode() == saved, "control signal failed to restore console mode");
    require(capture.buffer.saw_prompt && capture.buffer.prompt_hidden, "control-signal password prompt exposed echo");
}

void test_setup_child_process(const std::wstring& setup_exe) {
    test_helpers::TemporaryDirectory temp_dir;
    auto env_file = temp_dir.root / test_helpers::path("setup.env");
    auto config_file = temp_dir.root / test_helpers::path("setup.json");

    // 1. Non-terminal rejection: redirected input must cause setup to fail
    {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        HANDLE null_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        HANDLE null_out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        require(null_in != INVALID_HANDLE_VALUE && null_out != INVALID_HANDLE_VALUE, "open NUL handles");
        si.hStdInput = null_in;
        si.hStdOutput = null_out;
        si.hStdError = null_out;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + setup_exe + L"\"";
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(0);
        require(CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi),
                "spawn setup without terminal");
        DWORD wait_res = WaitForSingleObject(pi.hProcess, 5000);
        if (wait_res != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            CloseHandle(null_in);
            CloseHandle(null_out);
            require(false, "setup child process timed out waiting to exit on redirected input");
        }
        DWORD code = 0;
        require(GetExitCodeProcess(pi.hProcess, &code) && code != 0,
                "setup must exit non-zero when not connected to a terminal");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(null_in);
        CloseHandle(null_out);
    }

    // 2. Child process cancellation test
    {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + setup_exe + L"\" --env-path \"" + env_file.wstring() + L"\" --config-path \"" + config_file.wstring() + L"\"";
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(0);
        require(CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi),
                "spawn setup child in new process group");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        BOOL delivered = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
        require(delivered, "GenerateConsoleCtrlEvent delivery to setup child failed");
        DWORD wait_res = WaitForSingleObject(pi.hProcess, 5000);
        if (wait_res != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            require(false, "cancelled setup child process timed out without exiting");
        }
        DWORD code = 0;
        require(GetExitCodeProcess(pi.hProcess, &code) && code != 0,
                "cancelled setup child process should return non-zero exit code");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::wstring setup_exe;
        if (argc > 1) {
            setup_exe = test_helpers::path(argv[1]).wstring();
        }

        FreeConsole();
        require(AllocConsole(), "allocate native test console");
        input_handle = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        require(input_handle != INVALID_HANDLE_VALUE && output != INVALID_HANDLE_VALUE, "open native console");
        SetStdHandle(STD_INPUT_HANDLE, input_handle);
        SetStdHandle(STD_OUTPUT_HANDLE, output);
        SetStdHandle(STD_ERROR_HANDLE, output);
        require(SetConsoleMode(input_handle, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT), "initialize console mode");
        baseline = mode();
        const UINT saved_input_cp = GetConsoleCP(), saved_output_cp = GetConsoleOutputCP();
        DWORD saved_output_mode = 0;
        require(GetConsoleMode(output, &saved_output_mode), "save output mode");
        {
            itu::platform::ConsoleSession session;
            Capture capture;
            key(VK_UP);
            key(VK_RETURN, L'\r');
            require(show_menu("menu", {"one", "two", "three"}) == 2, "menu up must wrap");
            restored();

            key(VK_DOWN);
            key(VK_DOWN);
            key(VK_DOWN);
            key(VK_RETURN, L'\r');
            require(show_menu("menu", {"one", "two", "three"}) == 0, "menu down must wrap");
            restored();

            // Repeated key event regression (CP-19): single down event with repeat count 2
            key(VK_DOWN, 0, 0, 2);
            key(VK_RETURN, L'\r');
            require(show_menu("menu", {"zero", "one", "two", "three"}) == 2, "menu must process repeat count 2");
            restored();

            line(L"şifre-東京-🙂");
            std::string secret;
            require(itu::platform::read_line(secret, true, "PASSWORD-PROMPT"), "read Unicode password");
            require(secret == u8"şifre-東京-🙂", "cooked console UTF-8 conversion");
            require(capture.buffer.saw_prompt && capture.buffer.prompt_hidden, "password echo must be off before prompt and line editing retained");
            require(capture.buffer.str().find(secret) == std::string::npos, "password leaked to output");
            restored();

            line(L"normal-ü");
            std::string value;
            require(itu::platform::read_line(value) && value == u8"normal-ü", "normal Unicode line");
            restored();

            key('Z', 26, LEFT_CTRL_PRESSED);
            key(VK_RETURN, L'\r');
            bool ended = false;
            try {
                ended = !itu::platform::read_line(value, true, "PASSWORD-PROMPT");
            } catch (const std::exception&) {
                ended = true;
            }
            require(ended, "Ctrl-Z must report end of input");
            restored();
        }

        interrupt_case(CTRL_C_EVENT);
        restored();
        interrupt_case(CTRL_BREAK_EVENT);
        restored();

        {
            HANDLE read_pipe = nullptr, write_pipe = nullptr;
            require(CreatePipe(&read_pipe, &write_pipe, nullptr, 0), "create nonterminal input");
            SetStdHandle(STD_INPUT_HANDLE, read_pipe);
            bool refused = false;
            try {
                std::string value;
                itu::platform::read_line(value, true, "PASSWORD-PROMPT");
            } catch (const std::exception&) {
                refused = true;
            }
            SetStdHandle(STD_INPUT_HANDLE, input_handle);
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            require(refused, "nonterminal password input accepted");
            restored();
        }

        require(GetConsoleCP() == saved_input_cp && GetConsoleOutputCP() == saved_output_cp, "console code pages not restored");
        DWORD restored_output_mode = 0;
        require(GetConsoleMode(output, &restored_output_mode) && restored_output_mode == saved_output_mode, "console output mode not restored");

        test_helpers::TemporaryDirectory directory;
        auto destination = directory.root / test_helpers::path("秘密.env");
        require(itu::platform::atomic_write_private(destination, "first"), "private create");
        test_helpers::private_file(destination);
        require(itu::platform::atomic_write_private(destination, "second"), "private replacement");
        test_helpers::private_file(destination);

        if (!setup_exe.empty()) {
            test_setup_child_process(setup_exe);
        }

        CloseHandle(input_handle);
        CloseHandle(output);
        FreeConsole();
        return 0;
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        std::cerr << error.what() << '\n';
        return 1;
    }
}
