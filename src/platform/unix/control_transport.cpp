/**
 * @file src/platform/unix/control_transport.cpp
 * @brief The control channel on Linux and macOS: a unix socket, stream-service.sock in $XDG_RUNTIME_DIR (else the
 * config folder), open to its owner only.
 */
// standard includes
#include <cstdlib>
#include <filesystem>
#include <mutex>

// lib includes
#include <boost/asio.hpp>
#include <sys/stat.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/control_transport.h"

namespace platf::control {
  namespace {
    namespace asio = boost::asio;
    using local = asio::local::stream_protocol;

    std::string socket_path() {
      if (auto runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
        return (std::filesystem::path(runtime) / "stream-service.sock").string();
      }
      return (std::filesystem::path(config::sunshine.config_file).parent_path() / "stream-service.sock").string();
    }

    class socket_connection_t: public connection_t {
    public:
      explicit socket_connection_t(local::socket socket):
          socket {std::move(socket)} {}

      bool read_line(std::string &line) override {
        boost::system::error_code ec;
        asio::read_until(socket, buffer, '\n', ec);
        if (ec) {
          return false;
        }
        std::istream in {&buffer};
        std::getline(in, line);
        return true;
      }

      bool write(const std::string &text) override {
        std::lock_guard lock {writing};
        boost::system::error_code ec;
        asio::write(socket, asio::buffer(text), ec);
        return !ec;
      }

      void close() override {
        boost::system::error_code ec;
        socket.shutdown(local::socket::shutdown_both, ec);
        socket.close(ec);
      }

    private:
      local::socket socket;
      asio::streambuf buffer;
      std::mutex writing;
    };

    class socket_listener_t: public listener_t {
    public:
      socket_listener_t():
          path {socket_path()},
          acceptor {context} {
        std::filesystem::remove(path);
        acceptor.open(local());
        acceptor.bind(local::endpoint(path));
        chmod(path.c_str(), 0600);
        acceptor.listen();
      }

      ~socket_listener_t() override {
        close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }

      std::unique_ptr<connection_t> accept() override {
        boost::system::error_code ec;
        local::socket socket {context};
        acceptor.accept(socket, ec);
        if (ec) {
          return nullptr;
        }
        return std::make_unique<socket_connection_t>(std::move(socket));
      }

      void close() override {
        boost::system::error_code ec;
        acceptor.close(ec);
      }

      std::string address() const override {
        return path;
      }

    private:
      std::string path;
      asio::io_context context;
      local::acceptor acceptor;
    };
  }  // namespace

  std::unique_ptr<listener_t> listen() {
    try {
      return std::make_unique<socket_listener_t>();
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "control: the socket could not be made ("sv << e.what() << ')';
      return nullptr;
    }
  }
}  // namespace platf::control
