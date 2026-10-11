#pragma once

#include <cstdint>
#include <string>

namespace rebuild {

class SonyPlatform {
public:
    bool start();
    void stop();
    uint32_t buttons();
    bool keyboard(const char* title, std::string& value);
    void notify(const char* message) const;
    bool online();
    int get(const std::string& url, size_t limit, std::string& body);
    int userId() const { return userId_; }

private:
    int userId_ = -1;
    int pad_ = -1;
    int netPool_ = 0;
    int ssl_ = 0;
    int http_ = 0;
    bool dialogs_ = false;
};

}
