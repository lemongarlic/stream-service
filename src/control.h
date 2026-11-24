/**
 * @file src/control.h
 * @brief The control channel: how netcade (or any local program) drives stream-service in place of a web UI. JSON
 * lines over a local connection (see src/platform/control_transport.h): commands `{"id", "method", "args"}` answered
 * `{"id", "ok"}` or `{"id", "error"}`, and events `{"type", ...}` sent to every connection as they happen. The
 * commands and events are in STREAM-SERVICE.md.
 */
#pragma once

// lib includes
#include <nlohmann/json.hpp>

namespace control {
  /**
   * @brief Listens and serves until the program ends (a thread's body, as nvhttp::start is).
   */
  void start();

  /**
   * @brief Sends an event to every connection: `type`, and the fields given.
   */
  void event(const std::string &type, nlohmann::json fields = nlohmann::json::object());
}  // namespace control
