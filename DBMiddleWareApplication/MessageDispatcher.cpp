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
    register_handler("login", [this](std::shared_ptr<Session> session, const nlohmann::json& msg) {
        std::string nickname = msg.value("nickname", "");
        if (nickname.empty()) {
            session->post_write(R"({"type":"login_ack","result":"error","msg":"Nickname is required."})" "\n");
            return;
        }
        // 닉네임 중복 체크
        if (session_manager_->find_session_by_nickname(nickname)) {
            session->post_write(R"({"type":"login_ack","result":"error","msg":"Nickname already in use."})" "\n");
            return;
        }
        // 닉네임 등록
        session->set_nickname(nickname);
        session->on_nickname_registered();
        session_manager_->register_nickname(nickname, session);
        session->post_write(R"({"type":"login_ack","result":"ok"})" "\n");
        AppContext::instance().logger->info("[LOGIN] nickname={} session_id={}", nickname, session->get_session_id());
		});

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
    // 패킷이 secret 프리픽스로 시작하는지 먼저 판단
    const bool has_secret_prefix =
        packet.size() >= secret_.size() &&
        packet.compare(0, secret_.size(), secret_) == 0;

    nlohmann::json msg;

    if (!session->is_nickname_registered()) {
        // ── 로그인(닉네임 등록) 전: 반드시 secret + JSON ──
        if (!has_secret_prefix) {
            AppContext::instance().logger->warn("[SECURITY] (pre-login) secret missing. close. id={}", session->get_session_id());
            session->close_session();
            return;
        }
        const std::string json_str = packet.substr(secret_.size());
        try {
            msg = nlohmann::json::parse(json_str);
        }
        catch (const std::exception& e) {
            AppContext::instance().logger->warn("[SECURITY] (pre-login) JSON parse error: id={}, err={}", session->get_session_id(), e.what());
            session->close_session();
            return;
        }

        // 첫 패킷은 반드시 로그인
        if (msg.value("type", "") != "login") {
            AppContext::instance().logger->warn("[SECURITY] (pre-login) first packet must be type=login. id={}", session->get_session_id());
            session->close_session();
            return;
        }
    }
    else {
        // ── 로그인 이후: 원칙적으로 순수 JSON 허용, 단 keepalive는 secret 필수 ──
        std::string json_str;
        if (has_secret_prefix) {
            json_str = packet.substr(secret_.size());
        }
        else {
            json_str = packet; // 순수 JSON
        }

        try {
            msg = nlohmann::json::parse(json_str);
        }
        catch (const std::exception& e) {
            AppContext::instance().logger->warn("[SECURITY] (post-login) JSON parse error: id={}, err={}", session->get_session_id(), e.what());
            session->close_session();
            return;
        }

        // keepalive는 로그인 후에도 secret 프리픽스가 반드시 있어야 함
        const std::string type = msg.value("type", "");
        if (type == "keepalive" && !has_secret_prefix) {
            AppContext::instance().logger->warn("[SECURITY] keepalive without secret (post-login). close. id={}", session->get_session_id());
            session->close_session();
            return;
        }
    }

    // type 디스패치
    const std::string type = msg.value("type", "");
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
