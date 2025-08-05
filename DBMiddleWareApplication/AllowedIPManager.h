#pragma once
#include <unordered_set>
#include <string>

class AllowedIPManager {
public:
    bool load(const std::string& filepath); // IP 리스트 파일 읽기
    bool is_allowed(const std::string& ip) const; // IP 허용여부 체크

private:
    std::unordered_set<std::string> allowed_ips_;
};
