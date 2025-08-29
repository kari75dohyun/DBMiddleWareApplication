#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <queue>

// MySQL Connector/C++ 8.x (X DevAPI)
#include <mysqlx/xdevapi.h>

class MySqlPool {
public:
    MySqlPool(const std::string& host,
        unsigned int port,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        size_t pool_size);
    ~MySqlPool();

    // 복사/이동 금지
    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;
    MySqlPool(MySqlPool&&) = delete;
    MySqlPool& operator=(MySqlPool&&) = delete;

    // 세션 빌림/반납
    std::unique_ptr<mysqlx::Session> acquire();
    void release(std::unique_ptr<mysqlx::Session> session);

private:
    std::unique_ptr<mysqlx::Session> new_connection();

private:
    // 연결 정보
    std::string host_;
    unsigned int port_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    size_t      capacity_ = 0;

    std::mutex mtx_;
    std::queue<std::unique_ptr<mysqlx::Session>> pool_;
};
