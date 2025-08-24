#include "DataHandler.h" 
#include "Session.h"
#include <boost/asio.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include "Logger.h"
#include "Utility.h"
#include "SessionManager.h"
#include "MemoryTracker.h"
#include <string>
#include <chrono>
#include "AppContext.h"

using namespace std;
using namespace boost::asio;
using boost::asio::ip::udp;

DataHandler::DataHandler(boost::asio::io_context& io, std::shared_ptr<SessionManager> session_manager, const std::string& packet)
    : shard_count(max(4u, thread::hardware_concurrency() * 2)),
    dispatcher_(this, session_manager.get(), packet),
    monitor_timer_(io),
    session_manager_(session_manager),
    cleanup_timer_(io) {
    start_monitor_loop();    // 모니터 루프 시작
	//start_cleanup_loop();    // session 클린업 타이머 시작
}

//void DataHandler::dispatch(const std::shared_ptr<Session>& session, const json& msg) {
//    dispatcher_.dispatch(session, msg);
//}

void  DataHandler::dispatch(const std::shared_ptr<Session>& session, const std::string& packet) {
    dispatcher_.dispatch(session, packet);
}

void DataHandler::add_session(int session_id, std::shared_ptr<Session> session) {
    AppContext::instance().logger->info("[DEBUG][TCP] DataHandler address: {}", (void*)this);
    session_manager_->add_session(session);
}

void DataHandler::remove_session(int session_id) {
    // 1. SessionManager에서 제거하면서 세션 반환받기
    auto session = session_manager_->remove_session(session_id);

    if (!session) {
        AppContext::instance().logger->warn("[DataHandler] remove_session: session {} not found (already removed)", session_id);
        return;
    }

    // 3. 반환받은 세션을 세션 풀로 반환 (락 범위 밖에서 안전하게)
    //if (session) {
    //    auto pool = get_session_pool();
    //    AppContext::instance().logger->info("[DEBUG] get_session_pool() 호출됨, 주소: {}", (void*)pool.get());
    //    if (pool) {
    //        pool->release(session);  // is_closed() 여부와 상관없이 반드시 release
    //    }
    //}
}

std::shared_ptr<Session> DataHandler::find_session_by_nickname(const std::string& nickname) {
    return session_manager_->find_session_by_nickname(nickname);
}

// 전체 세션 순회
void DataHandler::for_each_session(const std::function<void(const std::shared_ptr<Session>&)> fn) {
    session_manager_->for_each_session(fn);
}

// 글로벌 keepalive 체크
void DataHandler::do_keepalive_check() {
    auto now = std::chrono::steady_clock::now();

    // 1. close해야 할 세션을 임시로 모아둘 벡터
    vector<shared_ptr<Session>> sessions_to_close;

    for_each_session([this, now, &sessions_to_close](shared_ptr<Session> sess) {
        if (!sess) return;

        if (sess->is_nickname_registered() == false) return; // 닉네임 등록 안된 세션은 skip

        });

    // 2. 락 해제 후(즉, 세션 안전하게 순회 후) 실제 close_session 호출
    for (auto& sess : sessions_to_close) {
        //cout << "[KEEPALIVE TIMEOUT] session_id=" << sess->get_session_id() << " - close session" << endl;
        AppContext::instance().logger->info("[KEEPALIVE TIMEOUT] session_id= {}", sess->get_session_id(), "- close session");
        sess->close_session();
    }
}

// 미인증 세션 정리 함수 
void DataHandler::cleanup_unauth_sessions(size_t max_unauth) {
    vector<shared_ptr<Session>> unauth_sessions;

    for_each_session([&](const shared_ptr<Session> sess) {
        if (!sess) return;
        SessionState state = sess->get_state();
        if (state == SessionState::Handshaking || state == SessionState::LoginWait) {
            unauth_sessions.push_back(sess);
        }
        });

    // 임계치 초과시, 오래된 세션부터 정리 (예: 타임스탬프 기준 정렬)
    if (unauth_sessions.size() > max_unauth) {
        // 오래된 순으로 정렬 (예: get_last_alive_time() 활용)
        sort(unauth_sessions.begin(), unauth_sessions.end(),
            [](const auto& a, const auto& b) {
                return a->get_last_alive_time() < b->get_last_alive_time();
            });

        size_t count_to_close = unauth_sessions.size() - max_unauth;
        for (size_t i = 0; i < count_to_close; ++i) {
            unauth_sessions[i]->close_session();
        }
    }
}

// 활성 세션 모니터링 루프 시작
void DataHandler::start_monitor_loop()
{
    monitor_timer_.expires_after(std::chrono::seconds(10)); // 60초마다 출력
    monitor_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            // 1. 전체 카운트
            AppContext::instance().logger->info("[SERVER] Active sessions: {}", session_manager_->get_total_session_count());

            MemoryTracker::log_memory_usage();   //메모리 사용량도 같이 남김

            start_monitor_loop(); // 반복
        }
        });
}


void DataHandler::start_cleanup_loop() {
    cleanup_timer_.expires_after(std::chrono::seconds(60)); // 1분마다
    cleanup_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            session_manager_->cleanup_inactive_sessions(std::chrono::seconds(300)); // 5분 이상 비활성 시 종료
            start_cleanup_loop(); // 재귀 호출
        }
        });
}

