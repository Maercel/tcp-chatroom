#include <iostream>
#include <thread>
#include <map>
#include <set>
#include <mutex>
#include <vector>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

constexpr int PORT = 54000;
constexpr int BUFFER_SIZE = 1024;

struct ClientInfo {
    int socket;
    std::string username;
    bool isAdmin = false;
};

ClientInfo adminInfo; 

std::map<int, ClientInfo> clients;
std::set<std::string> usernames;
std::mutex clientsMutex;

void sendLine(int sock, const std::string& msg) {
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


void broadcast(const std::string& msg, int excludeSock = -1) {
    std::vector<int> sockets;
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& [sock, _] : clients) {
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

void removeClient(int sock) {
    std::string name;
    bool wasAdmin = false;
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        if (!clients.count(sock)) return;
        
        name = clients[sock].username;
        wasAdmin = clients[sock].isAdmin; 

        usernames.erase(name);
        clients.erase(sock);
    }
    
    std::cout << "[Server] User '" << name << "' disconnected\n";
    broadcast("USER_LEFT|" + name);

    if (wasAdmin) {
        int newAdminSock = -1;
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if (!clients.empty()) {
                newAdminSock = clients.begin()->first;
                clients[newAdminSock].isAdmin = true;
            }
        }

        if (newAdminSock != -1) {
            sendLine(newAdminSock, "SYSTEM|You are now the admin");
            broadcast("SYSTEM|User '" + clients[newAdminSock].username + "' is now the admin", newAdminSock);
        }
    }

    if (sock >= 0) 
    if (close(sock) < 0) {
            std::cerr << "[Server] Failed to close socket when user disconnected. " << sock 
                    << ": " << strerror(errno) << std::endl;
            }
}

void handleClient(int clientSock) {
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
                std::lock_guard<std::mutex> lock(clientsMutex);
                if (clients.empty()) {
                    usernames.insert(username);
                    clients[clientSock] = {clientSock, username, true}; // admin
                    sendLine(clientSock, "JOIN_OK|ADMIN");
                    std::cout << "[Server] User '" << username << "' joined successfully\n";
                    insertUsername = true; 
                }
                else if (usernames.count(username)) {
                    sendLine(clientSock, "JOIN_FAIL|Username already taken");
                    std::cout << "Username taken\n";
                } else {
                    usernames.insert(username);
                    clients[clientSock] = {clientSock, username};
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
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (auto& [_, client] : clients) {
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
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    if (clients.count(clientSock)) {
                        isAdmin = clients[clientSock].isAdmin;
                    }
                }

                std::string role = isAdmin ? "ADMIN" : "USER";
                broadcast("MSG|" + role + "|" + username + "|" + text, clientSock);

            } else if (msg.rfind("KICK|", 0) == 0) {
                std::string targetUser = msg.substr(5);
                int targetSock = -1;

                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    
                    if (!clients[clientSock].isAdmin) {
                        sendLine(clientSock, "SYSTEM|You are not admin, cannot kick");
                        continue;
                    }

                    
                    for (auto& [sock, client] : clients) {
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
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (auto& [sock, client] : clients) {
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
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        if (clients.count(clientSock) && clients[clientSock].isAdmin)
                            role = "ADMIN";
                    }

                    sendLine(targetSock, "PRIVATE|" + role + "|" + clients[clientSock].username + "|" + privateText);

                    //sendLine(clientSock, "SYSTEM|Private message sent to '" + targetUser + "'");
                }
            }
        }

    } catch(const std::exception& e) {
        std::cerr << "[Server] Exception: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }
    
    int yes = 1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR\n";
        if (serverSock >= 0) {
            if (close(serverSock) < 0) {
                std::cerr << "[Server] Failed to close socket after SO_REUSEADDR failed. " << serverSock 
                        << ": " << strerror(errno) << std::endl;
            }
        }
        return -1;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Bind failed\n";
        return 1;
    }
    
    if (listen(serverSock, 10) < 0) {
        std::cerr << "Listen failed\n";
        return 1;
    }
    
    std::cout << "Server listening on port " << PORT << "\n";
    
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientLen);
        
        if (clientSock < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }
        
        std::cout << "[Server] Accepted connection on socket " << clientSock << "\n";
        std::thread(handleClient, clientSock).detach();
    }
    
    if (serverSock >= 0) {
        if (close(serverSock) < 0) {
            std::cerr << "[Server] Failed to close socket at the end of main()" << serverSock 
                    << ": " << strerror(errno) << std::endl;
        }
    }
    return 0;
}