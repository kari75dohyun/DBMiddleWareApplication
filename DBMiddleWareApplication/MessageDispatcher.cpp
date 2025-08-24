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
    register_handler("db.query", [](std::shared_ptr<Session> session, const nlohmann::json& msg) {
        try {
            auto db = AppContext::instance().db;
            if (!db) throw std::runtime_error("DB pool not initialized");
            std::string sql = msg.value("sql", "");
            if (sql.empty()) throw std::runtime_error("sql is empty");

            auto conn = db->acquire();
            std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql));

            // params: [ ... ]
            if (msg.contains("params") && msg["params"].is_array()) {
                int idx = 1;
                for (auto& p : msg["params"]) {
                    if (p.is_null())            ps->setNull(idx, 0);
                    else if (p.is_boolean())    ps->setBoolean(idx, p.get<bool>());
                    else if (p.is_number_integer()) ps->setInt64(idx, p.get<long long>());
                    else if (p.is_number_unsigned()) ps->setUInt64(idx, p.get<unsigned long long>());
                    else if (p.is_number_float())    ps->setDouble(idx, p.get<double>());
                    else                            ps->setString(idx, p.get<std::string>());
                    ++idx;
                }
            }

            bool is_select = false;
            {
                // 대충 앞부분 검사 (SELECT/SHOW/EXPLAIN 등 읽기)
                std::string head = sql;
                for (auto& c : head) c = ::toupper(static_cast<unsigned char>(c));
                is_select = head.find("SELECT") == 0 || head.find("SHOW") == 0 || head.find("EXPLAIN") == 0;
            }

            nlohmann::json out;
            if (is_select) {
                std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
                nlohmann::json rows = nlohmann::json::array();
                auto meta = rs->getMetaData();
                int cols = meta->getColumnCount();
                while (rs->next()) {
                    nlohmann::json row;
                    for (int i = 1; i <= cols; ++i) {
                        std::string col = meta->getColumnLabel(i);
                        row[col] = rs->getString(i); // 단순 문자열 변환(필요시 타입 변환 확장)
                    }
                    rows.push_back(std::move(row));
                }
                out = { {"type","db.query_ack"},{"ok",true},{"rows",rows} };
            }
            else {
                int affected = ps->executeUpdate();
                out = { {"type","db.query_ack"},{"ok",true},{"affected",affected} };
            }
            session->post_write(out.dump());
        }
        catch (const std::exception& e) {
            nlohmann::json err = { {"type","db.query_ack"},{"ok",false},{"error",e.what()} };
            session->post_write(err.dump());
        }
        });

    // 2) INSERT: {type:"db.insert", table:"t", values:{k:v,...}}
    register_handler("db.insert", [](std::shared_ptr<Session> session, const nlohmann::json& msg) {
        try {
            auto db = AppContext::instance().db;
            if (!db) throw std::runtime_error("DB pool not initialized");
            std::string table = msg.value("table", "");
            auto values = msg.value("values", nlohmann::json::object());
            if (table.empty() || !values.is_object() || values.empty())
                throw std::runtime_error("invalid table/values");

            std::ostringstream oss;
            std::ostringstream qmarks;
            bool first = true;
            std::vector<std::string> cols;
            for (auto& [k, v] : values.items()) {
                cols.push_back(k);
                if (!first) { oss << ","; qmarks << ","; }
                oss << k; qmarks << "?";
                first = false;
            }
            std::string sql = "INSERT INTO " + table + " (" + oss.str() + ") VALUES (" + qmarks.str() + ")";
            auto conn = db->acquire();
            std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql));

            for (size_t i = 0; i < cols.size(); ++i) {
                const auto& v = values[cols[i]];
                int idx = static_cast<int>(i + 1);
                if (v.is_null())                 ps->setNull(idx, 0);
                else if (v.is_boolean())         ps->setBoolean(idx, v.get<bool>());
                else if (v.is_number_integer())  ps->setInt64(idx, v.get<long long>());
                else if (v.is_number_unsigned()) ps->setUInt64(idx, v.get<unsigned long long>());
                else if (v.is_number_float())    ps->setDouble(idx, v.get<double>());
                else                              ps->setString(idx, v.get<std::string>());
            }
            int affected = ps->executeUpdate();
            nlohmann::json out = { {"type","db.insert_ack"},{"ok",true},{"affected",affected} };
            session->post_write(out.dump());
        }
        catch (const std::exception& e) {
            nlohmann::json err = { {"type","db.insert_ack"},{"ok",false},{"error",e.what()} };
            session->post_write(err.dump());
        }
        });

    // 3) SELECT 간편형: {type:"db.select", table:"t", columns:["a","b"], where:{k:v}, limit:10}
    register_handler("db.select", [](std::shared_ptr<Session> session, const nlohmann::json& msg) {
        try {
            auto db = AppContext::instance().db;
            if (!db) throw std::runtime_error("DB pool not initialized");
            std::string table = msg.value("table", "");
            if (table.empty()) throw std::runtime_error("table is empty");
            auto columns = msg.value("columns", nlohmann::json::array());
            auto where = msg.value("where", nlohmann::json::object());
            int  limit = msg.value("limit", 0);

            std::ostringstream col_oss;
            if (!columns.empty()) {
                bool f = true; for (auto& c : columns) { if (!f) col_oss << ","; col_oss << c.get<std::string>(); f = false; }
            }
            else {
                col_oss << "*";
            }

            std::ostringstream sql;
            sql << "SELECT " << col_oss.str() << " FROM " << table;

            std::vector<std::string> keys;
            if (!where.empty()) {
                sql << " WHERE ";
                bool f = true;
                for (auto& [k, v] : where.items()) {
                    if (!f) sql << " AND ";
                    sql << k << "=?";
                    keys.push_back(k);
                    f = false;
                }
            }
            if (limit > 0) sql << " LIMIT " << limit;

            auto conn = db->acquire();
            std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));

            for (size_t i = 0; i < keys.size(); ++i) {
                auto& v = where[keys[i]];
                int idx = static_cast<int>(i + 1);
                if (v.is_null())                 ps->setNull(idx, 0);
                else if (v.is_boolean())         ps->setBoolean(idx, v.get<bool>());
                else if (v.is_number_integer())  ps->setInt64(idx, v.get<long long>());
                else if (v.is_number_unsigned()) ps->setUInt64(idx, v.get<unsigned long long>());
                else if (v.is_number_float())    ps->setDouble(idx, v.get<double>());
                else                              ps->setString(idx, v.get<std::string>());
            }

            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            nlohmann::json rows = nlohmann::json::array();
            auto meta = rs->getMetaData();
            int cols = meta->getColumnCount();
            while (rs->next()) {
                nlohmann::json row;
                for (int i = 1; i <= cols; ++i) {
                    std::string col = meta->getColumnLabel(i);
                    row[col] = rs->getString(i);
                }
                rows.push_back(std::move(row));
            }
            nlohmann::json out = { {"type","db.select_ack"},{"ok",true},{"rows",rows} };
            session->post_write(out.dump());
        }
        catch (const std::exception& e) {
            nlohmann::json err = { {"type","db.select_ack"},{"ok",false},{"error",e.what()} };
            session->post_write(err.dump());
        }
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
