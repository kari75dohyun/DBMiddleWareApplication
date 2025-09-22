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
        // (선택) CRLF 파일 대응: 줄 끝 \r 제거
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 앞뒤 공백 트림(공백만 있으면 스킵)
        const char* ws = " \t\r\n";
        auto first = line.find_first_not_of(ws);
        if (first == std::string::npos) continue;  // 전부 공백 → 무시
        auto last = line.find_last_not_of(ws);
        line.erase(last + 1);
        line.erase(0, first);

        // 빈 줄/주석 라인 무시
        if (line.empty() || line[0] == '#') continue;

        // (선택) 라인 내 인라인 주석을 허용하고 싶다면:
        // auto hash = line.find('#');
        // if (hash != std::string::npos) {
        //     line.erase(hash);
        //     // 다시 트림
        //     first = line.find_first_not_of(ws);
        //     if (first == std::string::npos) continue;
        //     last  = line.find_last_not_of(ws);
        //     line.erase(last + 1);
        //     line.erase(0, first);
        //     if (line.empty()) continue;
        // }

        allowed_ips_.insert(line);
    }
    //while (std::getline(fin, line)) {
    //    // 앞뒤 공백 제거
    //    line.erase(0, line.find_first_not_of(" \t\r\n"));
    //    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    //    if (line.empty() || line[0] == '#') continue; // 빈 줄, 주석 무시
    //    allowed_ips_.insert(line);
    //}
    return true;
}

bool AllowedIPManager::is_allowed(const std::string& ip) const {
    return allowed_ips_.count(ip) > 0;
}
