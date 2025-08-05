#pragma once
#include "Session.h"   
#include <boost/asio.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "SessionManager.h"
#include "MessageDispatcher.h"   // 추가!
#include <boost/asio/strand.hpp>
#include <nlohmann/json.hpp>
#include "Utility.h"
#include <thread>
#include <boost/asio/steady_timer.hpp>

using json = nlohmann::json;

class Session;  // 전방 선언, Session 클래스가 정의되기 전에 사용
class SessionManager;


class DataHandler {
private:

    unsigned int shard_count = 0;

    // 함수포인터(람다) 기반 Dispatcher
    MessageDispatcher dispatcher_;

    std::shared_ptr<SessionManager> session_manager_; // SessionManager 멤버 추가

    std::unordered_map<std::string, std::weak_ptr<Session>> nickname_to_session_;  // 닉네임→세션
    std::mutex nickname_mutex_; // 닉네임 맵 보호용

    boost::asio::steady_timer monitor_timer_; // 모니터링 타이머

    boost::asio::steady_timer cleanup_timer_;  // 비활성 세션 클린업용 타이머


public:
    DataHandler(boost::asio::io_context& io, std::shared_ptr<SessionManager> session_manager, const std::string& packet); // 생성자 선언 필요!
    // *** 여기! 복사 금지 선언 추가 ***
    DataHandler(const DataHandler&) = delete;
    DataHandler& operator=(const DataHandler&) = delete;

    void dispatch(const std::shared_ptr<Session>& session, const json& msg);
    // TCP 세션 관리 
    // 세션 추가
    void add_session(int session_id, std::shared_ptr<Session> session);

    // 세션 제거
    void remove_session(int session_id);


    // 전체 세션을 정확하게 순회하는 함수(콜백 전달 방식)
    void for_each_session(const std::function<void(const std::shared_ptr<Session>&)> fn);

    std::shared_ptr<Session> find_session_by_nickname(const std::string& nickname);

    // 글로벌 keepalive 관련
    void do_keepalive_check();

	// 로그인 하지 않고 DDos 공격하는 세션 정리
    void cleanup_unauth_sessions(size_t max_unauth); // 미인증 세션 정리

	void start_monitor_loop(); // 모니터링 루프 시작 함수

    void start_cleanup_loop();  // 주기적 클린업 시작

};