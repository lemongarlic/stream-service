/**
 * @file src/platform/control_transport.h
 * @brief The control channel's transport (see src/control.h): local connections carrying lines of text, from one
 * listener. A named pipe on Windows, a unix socket elsewhere.
 */
#pragma once

// standard includes
#include <memory>
#include <string>

namespace platf::control {
  /**
   * @brief One client's connection. Reading and writing may happen at once, from two threads.
   */
  class connection_t {
  public:
    virtual ~connection_t() = default;

    /**
     * @brief The next line, without its newline; false once the connection's closed.
     */
    virtual bool read_line(std::string &line) = 0;

    /**
     * @brief Writes text (whole lines, newlines included); false once the connection's closed.
     */
    virtual bool write(const std::string &text) = 0;

    /**
     * @brief Closes it: a read or write waiting returns false.
     */
    virtual void close() = 0;
  };

  /**
   * @brief Where clients connect.
   */
  class listener_t {
  public:
    virtual ~listener_t() = default;

    /**
     * @brief The next client to connect, or nullptr once the listener's closed.
     */
    virtual std::unique_ptr<connection_t> accept() = 0;

    /**
     * @brief Stops listening: an accept waiting returns nullptr.
     */
    virtual void close() = 0;

    /**
     * @brief Where it listens, for the log.
     */
    virtual std::string address() const = 0;
  };

  /**
   * @brief Starts listening, local only; nullptr when it can't.
   */
  std::unique_ptr<listener_t> listen();
}  // namespace platf::control
