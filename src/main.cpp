/**
 * @file main.cpp
 * @brief Entry point for the Real-Time Audio-to-Subtitles application
 *
 * Orchestrates component initialization, signal handling, and the main
 * application lifecycle.
 */

#include <spdlog/spdlog.h>

#include "config/app_config.hpp"
#include "core/application.hpp"
#include "utils/signal_handler.hpp"

auto main(int argc, char* argv[]) -> int {
    // 1. Initialize Signal Handlers
    utils::SignalHandler::init();

    // 2. Parse Configuration
    AppConfig config = AppConfig::parse(argc, argv);

    // 3. Run Application
    Application app(config);
    return app.run();
}
