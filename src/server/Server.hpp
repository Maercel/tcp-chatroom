#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_set>
#include <string>

class Server {
public:
    explicit Server(uint16_t port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void acceptClients();

private:
    struct ClientInfo {
        int socket;
        std::string username;
        bool isAdmin = false;
    };

    uint16_t m_port;
    int m_serverSock = -1;

    std::map<int, ClientInfo> m_clients;
    std::unordered_set<std::string> m_usernames;
    // so const methods can lock
    mutable std::mutex m_clientsMutex;

    void sendLine(int sock, const std::string& msg) const noexcept;
    void broadcast(const std::string& msg, int excludeSock = -1) const;
    void removeClient(int sock);
    void handleClient(int clientSock);

    void clearConsole() const noexcept; 
};