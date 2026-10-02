#include "include/console.hpp"
#include "test_helpers.hpp"
#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>
#include <cstdio>
#include <vector>
#include <fstream>
#include "include/nlohmann_json.hpp"

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

// The real child shares this private console with the test. Read rendered
// prompts before sending input: timing sleeps cannot prove a reader is ready.
struct ConsoleSnapshot {
    DWORD input_mode = mode(), output_mode = 0;
    UINT input_cp = GetConsoleCP(), output_cp = GetConsoleOutputCP();
    ConsoleSnapshot() {
        require(GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode), "save child output mode");
    }
    void check() const {
        DWORD current_output = 0;
        require(mode() == input_mode, "setup child did not restore input mode");
        require(GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &current_output) && current_output == output_mode,
                "setup child did not restore output mode");
        require(GetConsoleCP() == input_cp && GetConsoleOutputCP() == output_cp,
                "setup child did not restore code pages");
    }
    void restore() const noexcept {
        SetConsoleMode(input_handle, input_mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode);
        SetConsoleCP(input_cp);
        SetConsoleOutputCP(output_cp);
    }
};

std::wstring screen() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    require(GetConsoleScreenBufferInfo(output, &info), "inspect child screen");
    std::wstring contents(static_cast<size_t>(info.dwSize.X) * info.dwSize.Y, L' ');
    DWORD count = 0;
    require(ReadConsoleOutputCharacterW(output, contents.data(), static_cast<DWORD>(contents.size()), {0, 0}, &count),
            "read child screen");
    contents.resize(count);
    return contents;
}

struct SetupChild {
    const ConsoleSnapshot saved_console;
    PROCESS_INFORMATION process{};
    SetupChild(const SetupChild&) = delete;
    SetupChild& operator=(const SetupChild&) = delete;
    SetupChild(const std::wstring& exe, const std::filesystem::path& env,
               const std::filesystem::path& config, const std::filesystem::path& cwd,
               bool redirected = false) {
        require(FlushConsoleInputBuffer(input_handle), "clear child input");
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        DWORD written = 0;
        require(GetConsoleScreenBufferInfo(output, &info) &&
                FillConsoleOutputCharacterW(output, L' ', static_cast<DWORD>(info.dwSize.X) * info.dwSize.Y,
                                            {0, 0}, &written) &&
                SetConsoleCursorPosition(output, {0, 0}), "clear child screen");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE null_in = INVALID_HANDLE_VALUE, null_out = INVALID_HANDLE_VALUE;
        if (redirected) {
            null_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, 0, nullptr);
            null_out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, 0, nullptr);
            if (null_in == INVALID_HANDLE_VALUE || null_out == INVALID_HANDLE_VALUE) {
                if (null_in != INVALID_HANDLE_VALUE) CloseHandle(null_in);
                if (null_out != INVALID_HANDLE_VALUE) CloseHandle(null_out);
                require(false, "open inheritable NUL handles");
            }
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = null_in;
            startup.hStdOutput = startup.hStdError = null_out;
        }
        std::wstring command = L"\"" + exe + L"\" --env-path \"" + env.wstring() +
                               L"\" --config-path \"" + config.wstring() + L"\"";
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(0);
        const BOOL created = CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, redirected,
                                           CREATE_NEW_PROCESS_GROUP, nullptr, cwd.c_str(), &startup, &process);
        if (null_in != INVALID_HANDLE_VALUE) CloseHandle(null_in);
        if (null_out != INVALID_HANDLE_VALUE) CloseHandle(null_out);
        require(created, "spawn setup child");
    }
    ~SetupChild() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
        // Assertions run before destruction, so cleanup cannot hide a failed
        // restoration check. It keeps the test console usable after a timeout.
        saved_console.restore();
        FlushConsoleInputBuffer(input_handle);
    }
    void prompt(const wchar_t* text) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            require(WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT, "setup exited before expected prompt");
            if (screen().find(text) != std::wstring::npos) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(false, "setup prompt readiness timed out");
    }
    void finish(bool success) {
        require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "setup child exit timed out");
        DWORD code = 0;
        require(GetExitCodeProcess(process.hProcess, &code) && (success ? code == 0 : code != 0),
                "unexpected setup child exit code");
    }
    void credentials() {
        prompt(L"1. Update user credentials");
        key(VK_RETURN, L'\r');
        prompt(L"Enter your username: ");
        line(L"öğrenci-東京");
        prompt(L"Enter your password: ");
        require(!(mode() & ENABLE_ECHO_INPUT) && (mode() & ENABLE_LINE_INPUT),
                "setup child password prompt exposed echo or lost line editing");
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "open child output file");
    std::ostringstream contents;
    contents << stream.rdbuf();
    require(!stream.bad(), "read child output file");
    return contents.str();
}

void test_setup_child_process(const std::wstring& setup_exe) {
    test_helpers::TemporaryDirectory directory;
    const auto env = directory.root / test_helpers::path(u8"秘密.env");
    const auto config = directory.root / test_helpers::path(u8"設定.json");
    const ConsoleSnapshot original;
    {
        SetupChild child(setup_exe, env, config, directory.root, true);
        child.finish(false);
        original.check();
        require(!std::filesystem::exists(env) && !std::filesystem::exists(config), "redirected setup wrote files");
    }
    {
        SetupChild child(setup_exe, env, config, directory.root);
        child.credentials();
        line(L"SECRET-MARKER-şifre-東京-🙂");
        child.finish(true);
        original.check();
        require(read_file(env) == u8"ITU_USERNAME=\"öğrenci-東京\"\nITU_PASSWORD=\"SECRET-MARKER-şifre-東京-🙂\"\n",
                "setup child UTF-8 credential round trip failed");
        test_helpers::private_file(env);
        require(screen().find(L"SECRET-MARKER") == std::wstring::npos, "setup child echoed password");
    }
    {
        SetupChild child(setup_exe, env, config, directory.root);
        child.prompt(L"2. Update add/drop list and time");
        key(VK_DOWN);
        key(VK_RETURN, L'\r');
        child.prompt(L"Enter date for course selection"); line(L"2030/02/28");
        child.prompt(L"Enter time of course selection"); line(L"10:20:30:456");
        child.prompt(L"Enter lead milliseconds"); line(L"0");
        child.prompt(L"Enter add CRNs"); line(L"12345, 67890");
        child.prompt(L"Enter drop CRNs"); line(L"54321");
        child.finish(true);
        original.check();
        const auto actual = nlohmann::json::parse(read_file(config));
        const auto expected = nlohmann::json::parse(R"({"time":{"year":2030,"month":2,"day":28,"hour":10,"minute":20,"second":30,"millisecond":456,"lead_millisecond":0},"courses":{"crn":["12345","67890"],"scrn":["54321"]}})");
        require(actual == expected, "setup child config round trip failed");
        test_helpers::private_file(config);
    }
    // Cancellation is observed after the actual password reader is ready, with
    // an existing destination so an accidental partial write is detectable.
    const auto saved_env = read_file(env), saved_config = read_file(config);
    {
        SetupChild child(setup_exe, env, config, directory.root);
        child.credentials();
        require(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, child.process.dwProcessId), "interrupt setup child");
        child.finish(false);
        original.check();
        require(read_file(env) == saved_env && read_file(config) == saved_config, "cancelled setup changed files");
    }
    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory.root)) {
        require(entry.path() == env || entry.path() == config, "setup left temporary files");
        ++files;
    }
    require(files == 2, "setup output files missing");
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::wstring setup_exe;
        const auto arguments = itu::platform::arguments(argc, argv);
        if (arguments.size() > 1) {
            setup_exe = std::filesystem::absolute(test_helpers::path(arguments[1])).wstring();
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
