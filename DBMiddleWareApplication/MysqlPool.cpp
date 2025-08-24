#include "MySqlPool.h"
#include <cppconn/exception.h>
#include <stdexcept>

MySqlPool::MySqlPool(const std::string& url,
    const std::string& user,
    const std::string& pass,
    const std::string& schema,
    size_t pool_size)
    : driver_(get_driver_instance()),
    url_(url), user_(user), pass_(pass), schema_(schema),
    capacity_(pool_size)
{
    if (!driver_) throw std::runtime_error("MySQL driver not found");
    std::unique_lock<std::mutex> lk(mtx_);
    for (size_t i = 0; i < capacity_; ++i) create_one_unlocked();
}

void MySqlPool::create_one_unlocked() {
    sql::Connection* c = driver_->connect(url_, user_, pass_);
    c->setSchema(schema_);
    pool_.push(c);
}

MySqlPool::ConnPtr MySqlPool::acquire() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [&] { return !pool_.empty(); });
    sql::Connection* raw = pool_.front(); pool_.pop();
    auto deleter = [this](sql::Connection* c) {
        std::lock_guard<std::mutex> g(mtx_);
        pool_.push(c);
        cv_.notify_one();
        };
    return ConnPtr(raw, deleter);
}

size_t MySqlPool::size() {
    std::lock_guard<std::mutex> lk(mtx_);
    return pool_.size();
}
