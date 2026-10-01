#include <iostream>
#include <cstring>
#include <thread>
#include <string>
#include <sstream>
#include <atomic>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <exception>

constexpr int PORT = 54000;
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int BUFFER_SIZE = 1024;

// ANSI colors
constexpr const char* COLOR_RESET = "\033[0m";
constexpr const char* COLOR_GREEN = "\033[32m";
constexpr const char* COLOR_BLUE = "\033[34m";
constexpr const char* COLOR_YELLOW = "\033[33m";
constexpr const char* COLOR_RED = "\033[31m";

// clear
constexpr const char* CLEAR_CONSOLE_LINE = "\33[2K";

std::atomic<bool> running(true);
std::string myUsername; // changed: receive thread needs it to redraw the prompt

void outputChatPrefix(std::string username) {
    if (username.empty()) throw std::invalid_argument("Username cannot be empty! ");
    std::cout << COLOR_BLUE << "[" << username << "]: " << COLOR_RESET << std::flush; // changed: no "\n", so typing stays on the same line
}

void receiveThread(int sock) {
    char buffer[BUFFER_SIZE];
    std::string leftover = "";
    
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            std::cout << COLOR_RED << "\nDisconnected from server.\n" << COLOR_RESET;
            running = false;
            break; //exit(0) before
        }
        
        buffer[bytes] = '\0';
        std::string data = leftover + std::string(buffer);
        leftover = "";

        std::cout << "\r" << CLEAR_CONSOLE_LINE; // changed: wipe the prompt line before printing incoming messages
        
        size_t pos = 0;
        while ((pos = data.find('\n')) != std::string::npos) {
            std::string msg = data.substr(0, pos);
            data = data.substr(pos + 1);
            
            if (msg.empty()) continue;
            
            // Remove \r
            if (!msg.empty() && msg.back() == '\r') {
                msg.pop_back();
            }
            
            if (msg.rfind("MSG|", 0) == 0) {
                // MSG|role|username|text
                size_t first = msg.find('|');
                size_t second = msg.find('|', first + 1);
                size_t third = msg.find('|', second + 1);

                if (first != std::string::npos && second != std::string::npos && third != std::string::npos) {
                    std::string role = msg.substr(first + 1, second - first - 1);
                    std::string username = msg.substr(second + 1, third - second - 1);
                    std::string text = msg.substr(third + 1);

                    if (role == "ADMIN") {
                        std::cout << COLOR_RED << "[" << username << "]: " << COLOR_RESET << text << "\n";
                    } else {
                        std::cout << COLOR_BLUE << "[" << username << "]: " << COLOR_RESET << text << "\n";
                    }
                }
            }
            else if (msg.rfind("USER_JOINED|", 0) == 0) {
                std::string username = msg.substr(12);
                std::cout << COLOR_GREEN << "[SYSTEM] User '" << username << "' joined the chat " << COLOR_RESET << "\n";
            }
            else if (msg.rfind("USER_LEFT|", 0) == 0) {
                std::string username = msg.substr(10);
                std::cout << COLOR_YELLOW << "[SYSTEM] User '" << username << "' left the chat " << COLOR_RESET << "\n";
            } else if (msg.rfind("USERS|", 0) == 0) {
                std::string list = msg.substr(6);

                std::cout << std::string(50, '-') << "\n";
                std::cout << "Connected users:\n";

                std::stringstream ss(list);
                std::string entry;

                while (std::getline(ss, entry, ',')) {
                    if (entry.rfind("ADMIN:", 0) == 0) {
                        std::cout << COLOR_RED
                                << entry.substr(6)
                                << " (admin)\n"
                                << COLOR_RESET;
                    } else if (entry.rfind("USER:", 0) == 0) {
                        std::cout << entry.substr(5) << "\n";
                    }
                }

                std::cout << std::string(50, '-') << "\n";
                
                } else if (msg.rfind("KICKED|", 0) == 0) {
                    std::string reason = msg.substr(7); 
                    std::cout << COLOR_RED << "[SYSTEM] " << reason << "\n" << COLOR_RESET;
                    running = false; 
                    break; 
                } else if (msg.rfind("SYSTEM|", 0) == 0) {
                    std::string sysMsg = msg.substr(7);
                    std::cout << COLOR_YELLOW << "[SYSTEM] " << sysMsg << COLOR_RESET << "\n";
                } else if (msg.rfind("PRIVATE|", 0) == 0) {
                    // PRIVATE|role|username|text
                    size_t first = msg.find('|');
                    size_t second = msg.find('|', first + 1);
                    size_t third = msg.find('|', second + 1);

                    if (first != std::string::npos && second != std::string::npos && third != std::string::npos) {
                        std::string role = msg.substr(first + 1, second - first - 1);
                        std::string username = msg.substr(second + 1, third - second - 1);
                        std::string text = msg.substr(third + 1);

                        if (role == "ADMIN") {
                            std::cout << COLOR_RED << "[PRIVATE][" << username << "]: " << COLOR_RESET << text << "\n";
                        } else {
                            std::cout << COLOR_BLUE << "[PRIVATE][" << username << "]: " << COLOR_RESET << text << "\n";
                        }
                    }
                } else {
                        // idk
                        std::cout << COLOR_YELLOW << "[SERVER] " << msg << "\n" << COLOR_RESET;
                    }
                }
            // any incompletee
            leftover = data;

            if (running) outputChatPrefix(myUsername); // changed: redraw the prompt under the new messages
        }
    }

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << COLOR_RED << "Connect failed\n" << COLOR_RESET;
        return 1;
    }
    
    std::cout << COLOR_GREEN << "Connected to server!\n" << COLOR_RESET;
    
    std::string username;
    while (true) {
        std::cout << COLOR_BLUE << "Enter username: " << COLOR_RESET;
        std::getline(std::cin, username);
        
        if (username.empty()) {
            std::cout << COLOR_RED << "Username cannot be empty!\n" << COLOR_RESET;
            continue;
        }
        
        std::string joinMsg = "JOIN|" + username + "\n";
        if (send(sock, joinMsg.c_str(), joinMsg.size(), 0) < 0) {
            std::cerr << "[Server] Failed to send to socket " << sock << ": " << strerror(errno) << std::endl; 
        }
        
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            std::cerr << COLOR_RED << "Server closed connection\n" << COLOR_RESET;
            if (sock >= 0)
            if (close(sock) < 0) {
            std::cerr << "Failed to close socket when server closed connection. " << sock 
                    << ": " << strerror(errno) << std::endl;
            }
            return 1;
        }
        
        buffer[bytes] = '\0';
        std::string response(buffer);
        
        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
            response.pop_back();
        }
        
        if (response == "JOIN_OK|ADMIN") {
            std::cout << COLOR_GREEN << "Joined chat successfully as '" << username << "'!\n" << COLOR_RESET;
            std::cout << COLOR_RED << "YOU ARE THE ADMIN!\n" << COLOR_RESET; 
            std::cout << COLOR_YELLOW << "Type '/quit' to exit\n" << COLOR_RESET;
            std::cout << std::string(50, '-') << "\n";
            break;
        }
        else if (response == "JOIN_OK") {
            std::cout << COLOR_GREEN << "Joined chat successfully as '" << username << "'!\n" << COLOR_RESET;
            std::cout << COLOR_YELLOW << "Type '/quit' to exit\n" << COLOR_RESET;
            std::cout << std::string(50, '-') << "\n";
            break;
        } else if (response.rfind("JOIN_FAIL|", 0) == 0) {
            std::string reason = response.substr(10);
            std::cout << COLOR_RED << "Failed to join: " << reason << "\n" << COLOR_RESET;
            //std::cout << COLOR_YELLOW << "Username already taken.\n" << COLOR_RESET;
        } else {
            std::cout << COLOR_RED << "Unexpected response: " << response << "\n" << COLOR_RESET;
        }
    }
    
    myUsername = username; // changed: set before the receive thread starts
    std::thread(receiveThread, sock).detach();
    
    while (running) {
        outputChatPrefix(username); // changed: uncommented

        std::string text;
        std::getline(std::cin, text);
        
        if (!running) break;
        
        if (text.empty()) continue;

        if (text == "/users") {
            std::string usersMsg = "USERS\n";
            if (send(sock, usersMsg.c_str(), usersMsg.size(), 0) < 0) {
                std::cerr << "Failed to send /users message to socket " << sock << ": " << strerror(errno) << std::endl; 
            }
            continue;
        }

        if (text == "/quit") {
            std::cout << COLOR_YELLOW << "QUITING\n" << COLOR_RESET;
            std::string quitMsg = "QUIT\n";
            if (send(sock, quitMsg.c_str(), quitMsg.size(), 0) < 0) {
                std::cerr << "Failed to send /quit message to socket " << sock << ": " << strerror(errno) << std::endl; 
            }
            if (sock >= 0)
            if (close(sock) < 0) {
            std::cerr << "Failed to close socket when quitting" << sock 
                    << ": " << strerror(errno) << std::endl;
            }
            running = false;
            break;
        }

        if (text.rfind("/kick ", 0) == 0) {
            std::string targetUser = text.substr(6); 
            if (targetUser.empty()) {
                std::cout << COLOR_RED << "Specify a user to kick!\n" << COLOR_RESET;
                continue;
            }

            std::string kickMsg = "KICK|" + targetUser + "\n";
            if (send(sock, kickMsg.c_str(), kickMsg.size(), 0) < 0) {
                std::cerr << "Failed to send kick command\n";
            }
            continue;
        }

        if (text.rfind("/private ", 0) == 0) {
            size_t firstSpace = text.find(' ', 9);
            if (firstSpace == std::string::npos) {
                std::cout << COLOR_RED << "Specify a user and a message for private!\n" << COLOR_RESET;
                continue;
            }

            std::string targetUser = text.substr(9, firstSpace - 9);
            std::string privateMsg = text.substr(firstSpace + 1);

            std::string msg = "PRIVATE|" + targetUser + "|" + privateMsg + "\n";
            if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
                std::cerr << "Failed to send private message\n";
            }
            continue;
        
        }

        std::string msg = "MSG|" + text + "\n";
        if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
            std::cerr << "Failed to send to socket " << sock << ": " << strerror(errno) << std::endl; 
        }
    }
    
    std::cout << COLOR_GREEN << "Goodbye!\n" << COLOR_RESET;
    if (sock >= 0) {
        if (close(sock) < 0) {
            std::cerr << "Failed to close socket at the end of main()" << sock 
                    << ": " << strerror(errno) << std::endl;
        }
    }
    
    return 0;
}