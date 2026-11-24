/**
 * @file src/control.cpp
 * @brief The control channel (see control.h): its connections, commands and events.
 */
// standard includes
#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

// local includes
#include "config.h"
#include "control.h"
#include "display_device.h"
#include "entry_handler.h"
#include "file_handler.h"
#include "globals.h"
#include "logging.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "platform/control_transport.h"
#include "process.h"

using namespace std::literals;

namespace control {
  namespace {
    using json = nlohmann::json;
    using connection_t = platf::control::connection_t;

    // the connections open now, for events
    std::mutex connections_mutex;
    std::vector<std::shared_ptr<connection_t>> connections;

    // a command's answer (thrown: its error)
    using command_t = std::function<json(const json &args)>;

    std::string text(const json &args, const char *key) {
      if (!args.contains(key) || !args[key].is_string()) {
        throw std::runtime_error("\""s + key + "\" missing");
      }
      return args[key].get<std::string>();
    }

    // the settings file's values, by name
    json config_values() {
      json out = json::object();
      for (auto &[key, value] : config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()))) {
        out[key] = value;
      }
      return out;
    }

    const std::map<std::string, command_t> &commands() {
      static const std::map<std::string, command_t> all {
        {"status", [](const json &) {
           return json {
             {"name", config::nvhttp.sunshine_name},
             {"version", PROJECT_VERSION},
             {"virtualDisplay", (int) proc::vDisplayDriverStatus},
           };
         }},
        // pairing: the PIN a client shows, and a name for it
        {"pin", [](const json &args) {
           return json(nvhttp::pin(text(args, "pin"), args.value("name", "")));
         }},
        // pairing without a PIN typed in: a one-time password for a passphrase the client's user enters
        {"otp", [](const json &args) {
           auto passphrase = text(args, "passphrase");
           if (passphrase.size() < 4) {
             throw std::runtime_error("the passphrase is too short");
           }
           return json {{"otp", nvhttp::request_otp(passphrase, args.value("name", ""))}, {"ip", platf::get_local_ip_for_gateway()}, {"name", config::nvhttp.sunshine_name}};
         }},
        {"clients", [](const json &) {
           return nvhttp::get_all_clients();
         }},
        {"unpair", [](const json &args) {
           return json(nvhttp::unpair_client(text(args, "uuid")));
         }},
        {"unpairAll", [](const json &) {
           nvhttp::erase_all_clients();
           proc::proc.terminate();
           return json(true);
         }},
        {"disconnect", [](const json &args) {
           return json(nvhttp::find_and_stop_session(text(args, "uuid"), true));
         }},
        // the settings file: all of it, and some of it changed (the rest kept; taken when the service restarts)
        {"config", [](const json &) {
           return config_values();
         }},
        {"setConfig", [](const json &args) {
           if (!args.contains("values") || !args["values"].is_object()) {
             throw std::runtime_error("\"values\" missing");
           }
           auto values = config_values();
           for (auto &[key, value] : args["values"].items()) {
             if (value.is_null() || (value.is_string() && value.get<std::string>().empty())) {
               values.erase(key);
             } else {
               values[key] = value.is_string() ? value.get<std::string>() : value.dump();
             }
           }
           std::stringstream out;
           for (auto &[key, value] : values.items()) {
             out << key << " = " << value.get<std::string>() << std::endl;
           }
           file_handler::write_file(config::sunshine.config_file.c_str(), out.str());
           return json(true);
         }},
        {"resetDisplayPersistence", [](const json &) {
           return json(display_device::reset_persistence());
         }},
        {"restart", [](const json &) {
           proc::proc.terminate();
           std::thread([] {
             std::this_thread::sleep_for(200ms);
             platf::restart();
           }).detach();
           return json(true);
         }},
        {"quit", [](const json &) {
           proc::proc.terminate();
           lifetime::exit_sunshine(0, true);
           return json(true);
         }},
      };
      return all;
    }

    std::string answer(const std::string &line) {
      json id = nullptr;
      try {
        auto request = json::parse(line);
        id = request.value("id", json(nullptr));
        auto method = request.value("method", ""s);
        auto command = commands().find(method);
        if (command == commands().end()) {
          return json {{"id", id}, {"error", "no command " + method}}.dump() + '\n';
        }
        return json {{"id", id}, {"ok", command->second(request.value("args", json::object()))}}.dump() + '\n';
      } catch (const std::exception &e) {
        return json {{"id", id}, {"error", e.what()}}.dump() + '\n';
      }
    }

    void serve(std::shared_ptr<connection_t> connection) {
      {
        std::lock_guard lock {connections_mutex};
        connections.push_back(connection);
      }
      std::string line;
      while (connection->read_line(line)) {
        if (!line.empty() && !connection->write(answer(line))) {
          break;
        }
      }
      connection->close();
      std::lock_guard lock {connections_mutex};
      std::erase(connections, connection);
    }
  }  // namespace

  void event(const std::string &type, nlohmann::json fields) {
    fields["type"] = type;
    auto line = fields.dump() + '\n';
    std::vector<std::shared_ptr<connection_t>> now;
    {
      std::lock_guard lock {connections_mutex};
      now = connections;
    }
    for (auto &connection : now) {
      connection->write(line);
    }
  }

  void start() {
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);
    auto listener = platf::control::listen();
    if (!listener) {
      return;
    }
    BOOST_LOG(info) << "control: listening at "sv << listener->address();
    std::shared_ptr<platf::control::listener_t> shared {std::move(listener)};
    std::thread stopper([shutdown_event, shared] {
      shutdown_event->view();
      shared->close();
      std::lock_guard lock {connections_mutex};
      for (auto &connection : connections) {
        connection->close();
      }
    });
    while (auto connection = shared->accept()) {
      std::thread(serve, std::shared_ptr<connection_t>(std::move(connection))).detach();
    }
    stopper.join();
  }
}  // namespace control
