#pragma once
#include <functional>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>

class Session;
class DataHandler;
class SessionManager; // 전방 선언

class MessageDispatcher {
public:
    using HandlerFunc = std::function<void(std::shared_ptr<Session>, const nlohmann::json&)>;
    using UdpHandlerFunc = std::function<void(std::shared_ptr<Session>, const nlohmann::json&, const boost::asio::ip::udp::endpoint&, boost::asio::ip::udp::socket&)>;

    MessageDispatcher(DataHandler* handler, SessionManager* sessionmanager, const std::string& secret); // DataHandler 포인터 주입

    void dispatch(std::shared_ptr<Session> session, const std::string& packet);
    //void dispatch(std::shared_ptr<Session> session, const nlohmann::json& msg);

    void register_handler(const std::string& type, HandlerFunc handler);

private:
    std::unordered_map<std::string, HandlerFunc> handlers_;
    DataHandler* handler_;
    SessionManager* session_manager_;
    std::string secret_;  // 시크릿 값 저장
};

