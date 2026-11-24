/**
 * @file src/platform/windows/control_transport.cpp
 * @brief The control channel on Windows: the named pipe \\.\pipe\stream-service, local clients only, open to
 * the system, administrators and the signed-in (interactive) user. Overlapped, so a read waiting doesn't hold up a
 * write.
 */
// standard includes
#include <atomic>
#include <mutex>

// platform includes
#include <windows.h>
// (after windows.h, which it needs)
#include <sddl.h>

// local includes
#include "src/logging.h"
#include "src/platform/control_transport.h"

using namespace std::literals;

namespace platf::control {
  namespace {
    constexpr auto PIPE_NAME = L"\\\\.\\pipe\\stream-service";
    constexpr DWORD BUFFER_SIZE = 64 * 1024;

    // waits for an overlapped operation, or the stop event; the bytes it moved, or -1 once stopped or failed
    long long finish(HANDLE handle, OVERLAPPED &ov, BOOL started, HANDLE stop) {
      if (!started && GetLastError() != ERROR_IO_PENDING) {
        return -1;
      }
      HANDLE waits[] {ov.hEvent, stop};
      if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(handle, &ov);
        DWORD ignored;
        GetOverlappedResult(handle, &ov, &ignored, TRUE);
        return -1;
      }
      DWORD bytes = 0;
      return GetOverlappedResult(handle, &ov, &bytes, FALSE) ? (long long) bytes : -1;
    }

    class pipe_connection_t: public connection_t {
    public:
      explicit pipe_connection_t(HANDLE pipe):
          pipe {pipe},
          stop {CreateEventW(nullptr, TRUE, FALSE, nullptr)},
          read_event {CreateEventW(nullptr, TRUE, FALSE, nullptr)},
          write_event {CreateEventW(nullptr, TRUE, FALSE, nullptr)} {}

      ~pipe_connection_t() override {
        close();
        CloseHandle(pipe);
        CloseHandle(stop);
        CloseHandle(read_event);
        CloseHandle(write_event);
      }

      bool read_line(std::string &line) override {
        for (;;) {
          if (auto end = pending.find('\n'); end != std::string::npos) {
            line = pending.substr(0, end);
            if (!line.empty() && line.back() == '\r') {
              line.pop_back();
            }
            pending.erase(0, end + 1);
            return true;
          }
          char buffer[4096];
          OVERLAPPED ov {};
          ov.hEvent = read_event;
          ResetEvent(read_event);
          auto got = finish(pipe, ov, ReadFile(pipe, buffer, sizeof(buffer), nullptr, &ov), stop);
          if (got <= 0) {
            return false;
          }
          pending.append(buffer, (std::size_t) got);
        }
      }

      bool write(const std::string &text) override {
        std::lock_guard lock {writing};
        std::size_t done = 0;
        while (done < text.size()) {
          OVERLAPPED ov {};
          ov.hEvent = write_event;
          ResetEvent(write_event);
          auto put = finish(pipe, ov, WriteFile(pipe, text.data() + done, (DWORD) (text.size() - done), nullptr, &ov), stop);
          if (put <= 0) {
            return false;
          }
          done += (std::size_t) put;
        }
        return true;
      }

      void close() override {
        if (!closed.exchange(true)) {
          SetEvent(stop);
          DisconnectNamedPipe(pipe);
        }
      }

    private:
      HANDLE pipe;
      HANDLE stop;
      HANDLE read_event;
      HANDLE write_event;
      std::string pending;
      std::mutex writing;
      std::atomic_bool closed {false};
    };

    class pipe_listener_t: public listener_t {
    public:
      pipe_listener_t():
          stop {CreateEventW(nullptr, TRUE, FALSE, nullptr)},
          connect_event {CreateEventW(nullptr, TRUE, FALSE, nullptr)} {
        // the system, administrators, and whoever's signed in at the PC (netcade runs as them)
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)", SDDL_REVISION_1, &descriptor, nullptr);
      }

      ~pipe_listener_t() override {
        close();
        CloseHandle(stop);
        CloseHandle(connect_event);
        if (descriptor) {
          LocalFree(descriptor);
        }
      }

      std::unique_ptr<connection_t> accept() override {
        SECURITY_ATTRIBUTES sa {sizeof(sa), descriptor, FALSE};
        auto pipe = CreateNamedPipeW(
          PIPE_NAME,
          PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
          PIPE_UNLIMITED_INSTANCES,
          BUFFER_SIZE,
          BUFFER_SIZE,
          0,
          descriptor ? &sa : nullptr
        );
        if (pipe == INVALID_HANDLE_VALUE) {
          BOOST_LOG(error) << "control: the pipe could not be made ("sv << GetLastError() << ')';
          return nullptr;
        }
        OVERLAPPED ov {};
        ov.hEvent = connect_event;
        ResetEvent(connect_event);
        auto connected = ConnectNamedPipe(pipe, &ov);
        if (!connected && GetLastError() == ERROR_PIPE_CONNECTED) {
          return std::make_unique<pipe_connection_t>(pipe);
        }
        if (finish(pipe, ov, connected, stop) < 0) {
          CloseHandle(pipe);
          return nullptr;
        }
        return std::make_unique<pipe_connection_t>(pipe);
      }

      void close() override {
        SetEvent(stop);
      }

      std::string address() const override {
        return R"(\\.\pipe\stream-service)";
      }

    private:
      HANDLE stop;
      HANDLE connect_event;
      PSECURITY_DESCRIPTOR descriptor {nullptr};
    };
  }  // namespace

  std::unique_ptr<listener_t> listen() {
    return std::make_unique<pipe_listener_t>();
  }
}  // namespace platf::control
