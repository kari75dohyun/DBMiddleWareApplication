#include "AllowedIPManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>

bool AllowedIPManager::load(const std::string& filepath) {
    allowed_ips_.clear();
    std::ifstream fin(filepath);
    if (!fin.is_open()) return false;

    std::string line;
    while (std::getline(fin, line)) {
        // 앞뒤 공백 제거
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue; // 빈 줄, 주석 무시
        allowed_ips_.insert(line);
    }
    return true;
}

bool AllowedIPManager::is_allowed(const std::string& ip) const {
    return allowed_ips_.count(ip) > 0;
}
