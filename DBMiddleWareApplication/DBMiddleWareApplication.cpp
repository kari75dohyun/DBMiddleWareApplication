#include "Server.h"
#include "Session.h"
#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include "Logger.h"
#include "Utility.h"
#include "AppContext.h"
#include "MySqlPool.h"
#include "generated/wire.pb.h"

using namespace std;
using boost::asio::ip::tcp;
using namespace boost::asio;

constexpr short DbMiddleWarePort = 6789; // 서버 포트

static std::string getenv_or(const char* k, const char* def) {
#ifdef _WIN32
    char* val = nullptr; size_t len = 0;
    if (_dupenv_s(&val, &len, k) || !val) return def;
    std::string s(val); free(val); return s;
#else
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string(def);
#endif
}

int main()
{
    init_logger();
    load_config();

    AppContext::instance().logger->info("=== DB MiddleWare 시작! ===");

    try {
        string secret = get_env_secret("MY_SERVER_SECRET");
        if (secret.empty()) {
            // 환경변수가 없으면 에러 처리!
            std::cerr << "비밀 환경변수가 설정되어 있지 않습니다!\n";
            AppContext::instance().logger->error("MY_SERVER_SECRET Error");
            return 0;
            // 프로그램 종료 또는 경고
        }

        // === DB 설정 ===
        std::string host = getenv_or("DB_HOST", "127.0.0.1");
        std::string port_str = getenv_or("DB_PORT", "33060");
        std::string user = getenv_or("DB_USER", "cppuser");
        std::string pass = getenv_or("DB_PASS", "cpppass");
        std::string schema = getenv_or("DB_NAME", "mydb");
        size_t pool_size = static_cast<size_t>(std::stoul(getenv_or("DB_POOL_SIZE", "8")));

        unsigned int port = static_cast<unsigned int>(std::stoul(port_str));

        AppContext::instance().config["db"] = {
            {"host", host}, {"port", port_str}, {"user", user}, {"schema", schema}, {"pool_size", pool_size}
        };

        AppContext::instance().db = std::make_shared<MySqlPool>(host, port, user, pass, schema, pool_size);
        AppContext::instance().logger->info("[DB] Pool ready. {} connections", pool_size);

        // 1. io_context 준비
        boost::asio::io_context io;

        AppContext::instance().logger->info("[DEBUG] 메인 io_context 주소: {}", (void*)&io);

        // 2. DataHandler 인스턴스 생성 (io를 전달)
        // DataHandler 객체 생성 및 공유 포인터로 관리
        auto session_manager = std::make_shared<SessionManager>(max(4u, thread::hardware_concurrency() * 2));
        
        auto data_handler = std::make_shared<DataHandler>(io, session_manager, secret);

        // 3. 세션풀, 서버 등 생성
        Server server(io, DbMiddleWarePort, data_handler);

        cout << "DB MiddleWare started on port: " << DbMiddleWarePort << endl;
        AppContext::instance().logger->info("DB MiddleWare started on port: {}", DbMiddleWarePort);

        // 4. 스레드 풀 및 io.run()
        size_t thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
        cout << "Thread count: " << thread_count << endl;
        //LOG_INFO("Thread count: ", thread_count);
        AppContext::instance().logger->info("Thread count: {}", thread_count);

        vector<thread> threads;
        for (size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([&io]() {
                try {
                    AppContext::instance().logger->info("[DEBUG] io.run() 스레드 진입!");  // ✅ 요기!
                    io.run();
                }
                catch (const std::exception& e) {
                    //std::cerr << "[FATAL] io_context.run()에서 예외 발생: " << e.what() << std::endl;
                    //LOG_ERROR("[FATAL] io_context.run()에서 예외 발생: ", e.what());
                    AppContext::instance().logger->error("[FATAL] io_context.run()에서 예외 발생: {}", e.what());
                    // 로그 남기고, 필요하다면 복구 시도
                }
                });
        }

        for (auto& t : threads)
            t.join();
    }
    catch (const std::exception& e) {
        AppContext::instance().logger->error("DB MiddleWare 시작 중 예외 발생: {}", e.what());
        return 1;
	}
}
