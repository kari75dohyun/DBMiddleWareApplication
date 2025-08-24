#pragma once
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <string>
#include <functional>

class MySqlPool {
public:
    using ConnPtr = std::unique_ptr<sql::Connection, std::function<void(sql::Connection*)>>;

    MySqlPool(const std::string& url,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        size_t pool_size);

    ConnPtr acquire(); // 자동 반환되는 RAII 핸들
    size_t size();
private:
    void create_one_unlocked();

    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<sql::Connection*> pool_;

    sql::Driver* driver_;
    std::string url_, user_, pass_, schema_;
    size_t capacity_;
};

