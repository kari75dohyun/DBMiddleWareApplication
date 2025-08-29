#include "MySqlPool.h"
#include "AppContext.h" // spdlog 헤더 대신 AppContext.h를 포함합니다.

MySqlPool::MySqlPool(const std::string& host,
    unsigned int port,
    const std::string& user,
    const std::string& pass,
    const std::string& schema,
    size_t pool_size)
    : host_(host), port_(port), user_(user), pass_(pass), schema_(schema),
    capacity_(pool_size)
{
    // spdlog::info -> AppContext::instance().logger->info
    AppContext::instance().logger->info("[MySqlPool] Initializing pool for {}@{}:{}/{} size={}", user_, host_, port_, schema_, capacity_);

    std::unique_lock<std::mutex> lk(mtx_);
    size_t ok = 0, fail = 0;
    for (size_t i = 0; i < capacity_; ++i) {
        try {
            pool_.push(new_connection());
            ++ok;
        }
        catch (const mysqlx::Error& e) {
            // spdlog::error -> AppContext::instance().logger->error
            AppContext::instance().logger->error("[MySqlPool] Slot {} init failed: {}", i, e.what());
            ++fail;
        }
        catch (const std::exception& e) {
            AppContext::instance().logger->error("[MySqlPool] Slot {} init failed (std::exception): {}", i, e.what());
            ++fail;
        }
    }

    // spdlog::info -> AppContext::instance().logger->info
    AppContext::instance().logger->info("[MySqlPool] Init done. capacity={}, created={}, failed={}", capacity_, ok, fail);
}

MySqlPool::~MySqlPool() = default;

// new_connection, acquire, release 함수는 변경할 필요가 없습니다.
std::unique_ptr<mysqlx::Session> MySqlPool::new_connection() {
    auto session = std::make_unique<mysqlx::Session>(host_, port_, user_, pass_);
    if (!schema_.empty()) {
        session->sql("USE " + schema_).execute();
    }
    return session;
}

std::unique_ptr<mysqlx::Session> MySqlPool::acquire() {
    std::unique_ptr<mysqlx::Session> session;
    {
        std::scoped_lock lk(mtx_);
        if (!pool_.empty()) {
            session = std::move(pool_.front());
            pool_.pop();
        }
    }
    if (!session) {
        try {
            session = new_connection();
        }
        catch (...) {
            AppContext::instance().logger->error("[MySqlPool] Failed to create a new connection on demand.");
            return nullptr;
        }
    }
    return session;
}

void MySqlPool::release(std::unique_ptr<mysqlx::Session> session) {
    if (!session) return;
    std::scoped_lock lk(mtx_);
    if (pool_.size() < capacity_) {
        pool_.push(std::move(session));
    }
}