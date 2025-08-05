#pragma once
#include "DataHandler.h"
#include <boost/asio.hpp>
#include <unordered_map>
#include <memory>
#include <atomic>
#include "AllowedIPManager.h"

class Server {
private:
    boost::asio::ip::tcp::acceptor acceptor_;

    std::atomic<int> session_counter_;
    std::shared_ptr<DataHandler> data_handler_;

    AllowedIPManager allowed_ip_mgr_;

public:
    Server(boost::asio::io_context& io, short port, std::shared_ptr<DataHandler> data_handler);

    void accept();

private:
    void start_accept();
};