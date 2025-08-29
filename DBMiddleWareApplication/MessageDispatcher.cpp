#include <iostream>
#include "MessageDispatcher.h"
#include "Session.h"
#include "DataHandler.h"
#include "Logger.h"
#include <memory>
#include "Utility.h"
#include "AppContext.h"
#include "MysqlPool.h"

MessageDispatcher::MessageDispatcher(DataHandler* handler, SessionManager* sessionmanager, const std::string& secret) : handler_(handler), session_manager_(sessionmanager), secret_(secret) {
    ///////////// TCP 메시지 핸들러 등록 /////////////
    // 1) GENERIC: 미리 준비한 SQL + params 바인딩

    register_handler("insert", [this](std::shared_ptr<Session> session, const nlohmann::json& msg) {
        AppContext::instance().logger->info("[DEBUG] handler msg: {}", msg.dump());
        std::string table = msg.value("table", "");
        nlohmann::json values = msg.value("values", nlohmann::json::object());
        AppContext::instance().logger->info("[DEBUG] handler values: {}", values.dump());

        std::ostringstream oss;
        oss << "INSERT INTO " << table << " (";
        bool first = true;
        for (auto& [k, v] : values.items()) {
            AppContext::instance().logger->info("[DEBUG][insert] key={}, type={}", k, v.type_name());
            if (!first) oss << ", ";
            oss << k;
            first = false;
        }
        oss << ") VALUES (";
        first = true;
        for (auto& [k, v] : values.items()) {
            if (!first) oss << ", ";
            // 타입에 상관없이 string으로 변환
            if (v.is_string()) {
                oss << "'" << v.get<std::string>() << "'";
            }
            else {
                oss << "'" << v.dump() << "'";
            }
            first = false;
        }
        oss << ");";
        std::string query = oss.str();
        AppContext::instance().logger->info("[insert handler] SQL: {}", query);
        session->post_write(R"({"type":"insert_ack","result":"ok"})" "\n");
        });


    //register_handler("insert", [this](std::shared_ptr<Session> session, const nlohmann::json& msg) {
    //    // (1) 필요한 값 추출
    //    std::string table = msg.value("table", "");
    //    nlohmann::json values = msg.value("values", nlohmann::json::object());

    //    // (2) 쿼리문 생성 (실제로는 SQL 인젝션 방지 필요!)
    //    std::ostringstream oss;
    //    oss << "INSERT INTO " << table << " (";
    //    bool first = true;
    //    for (auto& [k, v] : values.items()) {
    //        if (!first) oss << ", ";
    //        oss << k;
    //        first = false;
    //    }
    //    oss << ") VALUES (";
    //    first = true;
    //    for (auto& [k, v] : values.items()) {
    //        if (!first) oss << ", ";
    //        oss << "'" << v.get<std::string>() << "'";
    //        first = false;
    //    }
    //    oss << ");";
    //    std::string query = oss.str();

    //    AppContext::instance().logger->info("[insert handler] SQL: {}", query);

    //    // (3) 실제로는 MySQL 연동해서 쿼리 실행 예정 (아직 구현 안 했음)

    //    // (4) 클라이언트에 응답
    //    session->post_write(R"({"type":"insert_ack","result":"ok"})" "\n");
    //    });
    //////////////////////////////////////////////////
}

void MessageDispatcher::dispatch(std::shared_ptr<Session> session, const std::string& packet) {

    // 1. secret 검증
    if (packet.size() < secret_.size() || packet.compare(0, secret_.size(), secret_) != 0) {
        AppContext::instance().logger->warn("[SECURITY] 잘못된 secret, session 종료! id={}", session->get_session_id());
        session->close_session();
        return;
    }

    // 2. secret 뒤의 JSON만 파싱
    std::string json_str = packet.substr(secret_.size());
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(json_str);
    }
    catch (const std::exception& e) {
        AppContext::instance().logger->warn("[SECURITY] JSON 파싱 에러, session 종료! id={}, err={}", session->get_session_id(), e.what());
        session->close_session();
        return;
    }

    // 3. type별 핸들러 호출
    std::string type = msg.value("type", "");
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        it->second(session, msg);
    }
    else {
        session->post_write(R"({"type":"error","msg":"Unknown message type."})" "\n");
    }
}

void MessageDispatcher::register_handler(const std::string& type, HandlerFunc handler) {
    handlers_[type] = handler;
}
