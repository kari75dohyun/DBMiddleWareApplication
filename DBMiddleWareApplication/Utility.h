#pragma once
#include "Session.h"
#include <string>
#include <random>
#include <sstream>
#include <boost/asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include "Logger.h"
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include "AppContext.h"


class Session; // 전방 선언

// 어디서든 include해서 쓸 수 있도록!
inline std::string generate_random_token(size_t length = 32) {
    static const char hex_chars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i) {
        oss << hex_chars[dis(gen)];
    }
    return oss.str();
}

void send_admin_alert(const std::string& message);

// JSON 파싱 헬퍼
inline std::optional<nlohmann::json> try_parse_json(const std::string& msg) {
    try {
        return nlohmann::json::parse(msg);
    }
    catch (const std::exception& e) {
        AppContext::instance().logger->info("[UDP] JSON Parse failed: {} / original: {}", e.what(), msg);
        return std::nullopt;
    }
}

std::string get_env_secret(const std::string& env_name);
