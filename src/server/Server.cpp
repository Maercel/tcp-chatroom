#include "Server.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

constexpr int BUFFER_SIZE = 1024;

// ANSI codes
constexpr const char* CLEAR_SCREEN = "\033[2J";   
constexpr const char* CLEAR_SCROLLBACK = "\033[3J"; // erase scroll history (so the user can't go up in terminal)
constexpr const char* CURSOR_HOME = "\033[H";   // cursor to top-left of the terminal

Server::Server(uint16_t port) : m_port(port) {

    clearConsole(); 

    m_serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSock < 0) {
        throw std::runtime_error("Socket creation failed");
    }
    
    int yes = 1;
    if (setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        if (m_serverSock >= 0) {
            if (close(m_serverSock) < 0) {
                std::cerr << "[Server] Failed to close socket after SO_REUSEADDR failed. " << m_serverSock 
                        << ": " << strerror(errno) << std::endl;
            }
        }
        throw std::runtime_error("Failed to set SO_REUSEADDR");
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(m_serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        throw std::runtime_error("Bind failed");
    }
    
    if (listen(m_serverSock, 10) < 0) {
        throw std::runtime_error("Listen failed");
    }
}

Server::~Server() {
    if (m_serverSock >= 0) {
        if (close(m_serverSock) < 0) {
            std::cerr << "[Server] Failed to close socket at the end of main()" << m_serverSock 
                    << ": " << strerror(errno) << std::endl;
        }
    }
}

void Server::acceptClients() {
    std::cout << "Server listening on port " << m_port << "\n";
    
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(m_serverSock, (sockaddr*)&clientAddr, &clientLen);
        
        if (clientSock < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }
        
        std::cout << "[Server] Accepted connection on socket " << clientSock << "\n";
        std::thread(&Server::handleClient, this, clientSock).detach();
    }
}

void Server::clearConsole() const noexcept {
    std::cout << CLEAR_SCROLLBACK << CLEAR_SCREEN << CURSOR_HOME << std::flush;
}


void Server::sendLine(int sock, const std::string& msg) const noexcept {
    try {
        std::string fullMsg = msg;
        if (!fullMsg.empty() && fullMsg.back() != '\n') fullMsg += '\n';
        ssize_t sent = send(sock, fullMsg.c_str(), fullMsg.size(), 0);
        if (sent < 0) {
            std::cerr << "[Server] Failed to send to socket " << sock << ": " << strerror(errno) << std::endl;
        }
    } catch (...) {
        std::cerr << "[Server] Exception in sendLine for socket " << sock << std::endl;
    }
}


void Server::broadcast(const std::string& msg, int excludeSock) const {
    std::vector<int> sockets;
    
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& [sock, _] : m_clients) {
            if (sock != excludeSock) {
                sockets.push_back(sock);
            }
        }
    }
    
    std::string fullMsg = msg;
    if (fullMsg.back() != '\n') {
        fullMsg += '\n';
    }
    
    for (int sock : sockets) {
        sendLine(sock, fullMsg);
    }
}

void Server::removeClient(int sock) {
    std::string name;
    bool wasAdmin = false;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        if (!m_clients.count(sock)) return;

        
        
        name = m_clients[sock].username;
        wasAdmin = m_clients[sock].isAdmin; 

        m_usernames.erase(name);
        m_clients.erase(sock);
    }
    
    std::cout << "[Server] User '" << name << "' disconnected\n";
    broadcast("USER_LEFT|" + name);

    if (wasAdmin) {
        int newAdminSock = -1;
        std::string newAdminName; 
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            if (!m_clients.empty()) {
                newAdminSock = m_clients.begin()->first;
                m_clients[newAdminSock].isAdmin = true;
                newAdminName = m_clients.at(newAdminSock).username; 
            }
        }

         if (newAdminSock != -1) {
            sendLine(newAdminSock, "ROLE|ADMIN");
            broadcast("SYSTEM|User '" + newAdminName+ "' is now the admin", newAdminSock);
        }
    }

    if (sock >= 0) 
    if (close(sock) < 0) {
            std::cerr << "[Server] Failed to close socket when user disconnected. " << sock 
                    << ": " << strerror(errno) << std::endl;
            }
}

void Server::handleClient(int clientSock) {
    try {
        char buffer[BUFFER_SIZE];
        std::string username;
        
        std::cout << "[Server] New connection: socket " << clientSock << "\n";
        
        while (true) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes == 0) {  // client disconnected
                std::cout << "[Server] Client disconnected before joining\n";
                if (clientSock >= 0) {
                    if (close(clientSock) < 0) {
                        std::cerr << "[Server] Failed to close socket after client disconnected. " << clientSock 
                                << ": " << strerror(errno) << std::endl;
                    }
                }
                return;
            }
            if (bytes < 0) {  // error
                if (clientSock >= 0) {
                    if (close(clientSock) < 0) {
                        std::cerr << "[Server] Failed to close socket when reading bytes during JOIN phase. " << clientSock 
                                << ": " << strerror(errno) << std::endl;
                    }
                }
                throw std::runtime_error("Error reading bytes from client socket during JOIN phase. ");
            }
            
            buffer[bytes] = '\0';
            std::string msg(buffer);
            
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                msg.pop_back();
            }
            
            std::cout << "[Server] Received: '" << msg << "'\n";
            
            if (msg.rfind("JOIN|", 0) != 0) {
                sendLine(clientSock, "JOIN_FAIL|Invalid protocol");
                continue;
            }
            
            username = msg.substr(5);
            
            if (username.empty()) {
                sendLine(clientSock, "JOIN_FAIL|Username cannot be empty");
                continue;
            }
            
            bool insertUsername = false;
            bool makeAdmin = false; 

            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                if (m_clients.empty()) {
                    m_usernames.insert(username);
                    m_clients[clientSock] = {clientSock, username, true}; // admin
                    sendLine(clientSock, "JOIN_OK|ADMIN");
                    std::cout << "[Server] User '" << username << "' joined successfully\n";
                    insertUsername = true; 
                }
                else if (m_usernames.count(username)) {
                    sendLine(clientSock, "JOIN_FAIL|Username already taken");
                    std::cout << "Username taken\n";
                } else {
                    m_usernames.insert(username);
                    m_clients[clientSock] = {clientSock, username};
                    sendLine(clientSock, "JOIN_OK");
                    std::cout << "[Server] User '" << username << "' joined successfully\n";
                    insertUsername = true; 
                }
            } 

            if (insertUsername) {
                broadcast("USER_JOINED|" + username, clientSock);
                break;
            }
        }
        
        while (true) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes == 0) {  // client disconnected
                std::cout << "[Server] Connection lost with '" << username << "'\n";
                removeClient(clientSock);
                return;
            }
            if (bytes < 0) { // error
                removeClient(clientSock);
                throw std::runtime_error("Error reading bytes from client socket during MSG phase. ");
            }
            
            buffer[bytes] = '\0';
            std::string msg(buffer);
            
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                msg.pop_back();
            }
            
            std::cout << "[Server] From '" << username << "': '" << msg << "'\n";

            if (msg == "USERS") {
                
                std::ostringstream oss;
                oss << "USERS|";

                bool first = true;
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    for (auto& [_, client] : m_clients) {
                        if (!first) oss << ",";
                        first = false;

                        oss << (client.isAdmin ? "ADMIN:" : "USER:")
                            << client.username;
                    }
                }

                sendLine(clientSock, oss.str());
                continue;
            }
            
            if (msg == "QUIT") {
                std::cout << "[Server] User '" << username << "' quit\n";
                removeClient(clientSock);
                return;
            }
            
            if (msg.rfind("MSG|", 0) == 0) {
                std::string text = msg.substr(4);

                bool isAdmin = false;
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    if (m_clients.count(clientSock)) {
                        isAdmin = m_clients[clientSock].isAdmin;
                    }
                }

                std::string role = isAdmin ? "ADMIN" : "USER";
                broadcast("MSG|" + role + "|" + username + "|" + text, clientSock);

            } else if (msg.rfind("KICK|", 0) == 0) {
                std::string targetUser = msg.substr(5);
                int targetSock = -1;

                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    
                    if (!m_clients[clientSock].isAdmin) {
                        sendLine(clientSock, "SYSTEM|You are not admin, cannot kick");
                        continue;
                    }

                    
                    for (auto& [sock, client] : m_clients) {
                        if (client.username == targetUser) {
                            targetSock = sock;
                            break;
                        }
                    }
                }

                if (targetSock == -1) {
                    sendLine(clientSock, "SYSTEM|User not found");
                } else {
                    sendLine(targetSock, "KICKED|You were removed by admin");
                    removeClient(targetSock); // close socket and remove from list

                    sendLine(clientSock, "SYSTEM|User '" + targetUser + "' has been kicked");
                    broadcast("SYSTEM|User '" + targetUser + "' was kicked by admin", clientSock);
                }
            }
            else if (msg.rfind("PRIVATE|", 0) == 0) {
                // PRIVATE|targetUser|message
                size_t firstPipe = msg.find('|');
                size_t secondPipe = msg.find('|', firstPipe + 1);

                if (firstPipe == std::string::npos || secondPipe == std::string::npos) {
                    sendLine(clientSock, "SYSTEM|Invalid private message format");
                    continue;
                }

                std::string targetUser = msg.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                std::string privateText = msg.substr(secondPipe + 1);

                int targetSock = -1;
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    for (auto& [sock, client] : m_clients) {
                        if (client.username == targetUser) {
                            targetSock = sock;
                            break;
                        }
                    }
                }

                if (targetSock == -1) {
                    sendLine(clientSock, "SYSTEM|User not found for private message");
                } else {
                    std::string role = "USER";
                    {
                        std::lock_guard<std::mutex> lock(m_clientsMutex);
                        if (m_clients.count(clientSock) && m_clients[clientSock].isAdmin) 
                            role = "ADMIN";
                    }

                    sendLine(targetSock, "PRIVATE|" + role + "|" + username + "|" + privateText);
                }
            }
        }

    } catch(const std::exception& e) {
        std::cerr << "[Server] Exception: " << e.what() << std::endl;
    }
}