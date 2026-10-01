#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class Client {
public:
    Client(std::string serverIp, uint16_t port);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    int run();

private:
    std::string m_serverIp;
    uint16_t m_port;
    int m_sock = -1;
    std::string m_username;

    std::atomic<bool> m_isAdmin{false};
    std::atomic<bool> m_running{true};
    std::thread m_receiver;

    void outputChatPrefix(const std::string& username) const;
    void receiveThread();

    void clearConsole() const noexcept;
};