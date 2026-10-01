#include "Client.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

    constexpr int BUFFER_SIZE = 1024;

    // ANSI colors
    constexpr const char* COLOR_RESET = "\033[0m";
    constexpr const char* COLOR_GREEN = "\033[32m";
    constexpr const char* COLOR_BLUE = "\033[34m";
    constexpr const char* COLOR_YELLOW = "\033[33m";
    constexpr const char* COLOR_RED = "\033[31m";
    constexpr const char* COLOR_CYAN = "\033[36m";
    constexpr const char* COLOR_WHITE_HIGH_INTENSITY = "\033[97m";

    // ANSI codes
    constexpr const char* CURSOR_LINE_START = "\r";          // cursor to the start of the current line
    constexpr const char* CLEAR_LINE = "\033[2K";     // erase current line
    constexpr const char* CLEAR_SCREEN = "\033[2J";
    constexpr const char* CLEAR_SCROLLBACK = "\033[3J";   // erase scroll history (so the user can't go up in terminal)
    constexpr const char* CURSOR_HOME = "\033[H";    // cursor to top-left of the terminal

} 

Client::Client(std::string serverIp, uint16_t port) : m_serverIp(std::move(serverIp)), m_port(port) {}

Client::~Client() {
    if (m_sock >= 0) {
        if (close(m_sock) < 0) {
            std::cerr << "Failed to close socket at the end of main()" << m_sock
                    << ": " << strerror(errno) << std::endl;
        }
    }
}

void Client::clearConsole() const noexcept {
    std::cout << CLEAR_SCROLLBACK << CLEAR_SCREEN << CURSOR_HOME << std::flush;
}

void Client::outputChatPrefix(const std::string& username) const {
    if (username.empty()) throw std::invalid_argument("Username cannot be empty! ");
    const char* prefixColor = m_isAdmin ? COLOR_RED : COLOR_CYAN;
    std::cout <<  prefixColor << "[" << username << "]: " << COLOR_RESET << std::flush;
}

void Client::receiveThread() {
    char buffer[BUFFER_SIZE];
    std::string leftover = "";

    while (m_running) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(m_sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            if (m_running.exchange(false)) {
                std::cout << COLOR_RED << "\nDisconnected from server.\n" << COLOR_RESET;
            }
            break;
        }

        buffer[bytes] = '\0';
        std::string data = leftover + std::string(buffer);
        leftover = "";

        std::cout << CURSOR_LINE_START << CLEAR_LINE;

        size_t pos = 0;
        while ((pos = data.find('\n')) != std::string::npos) {
            std::string msg = data.substr(0, pos);
            data = data.substr(pos + 1);

            if (msg.empty()) continue;

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
                        std::cout << COLOR_WHITE_HIGH_INTENSITY << "[" << username << "]: " << COLOR_RESET << text << "\n";
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
                m_running = false;
                break;
             } else if (msg == "ROLE|ADMIN") {
                m_isAdmin = true;
                std::cout << COLOR_RED << "[SYSTEM] You are now the admin" << COLOR_RESET << "\n";
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
                        std::cout << COLOR_WHITE_HIGH_INTENSITY << "[PRIVATE][" << username << "]: " << COLOR_RESET << text << "\n";
                    }
                }
            } else {
                std::cout << COLOR_YELLOW << "[SERVER] " << msg << "\n" << COLOR_RESET;
            }
        }
        // any incompletee
        leftover = data;

        // prompt under messages
        if (m_running) outputChatPrefix(m_username);
    }
}

int Client::run() {

    clearConsole();

    m_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_port);
    inet_pton(AF_INET, m_serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(m_sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << COLOR_RED << "Connect failed\n" << COLOR_RESET;
        return 1;
    }

    std::cout << COLOR_GREEN << "Connected to server!\n" << COLOR_RESET;

    std::string username;
    while (true) {
        std::cout << COLOR_WHITE_HIGH_INTENSITY << "Enter username: " << COLOR_RESET;
        if (!std::getline(std::cin, username)) return 1;

        if (username.empty()) {
            std::cout << COLOR_RED << "Username cannot be empty!\n" << COLOR_RESET;
            continue;
        }

        std::string joinMsg = "JOIN|" + username + "\n";
        if (send(m_sock, joinMsg.c_str(), joinMsg.size(), 0) < 0) {
            std::cerr << "Failed to send to socket " << m_sock << ": " << strerror(errno) << std::endl;
        }

        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(m_sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            std::cerr << COLOR_RED << "Server closed connection\n" << COLOR_RESET;
            return 1;
        }

        buffer[bytes] = '\0';
        std::string response(buffer);

        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
            response.pop_back();
        }

        if (response == "JOIN_OK|ADMIN") {
            m_isAdmin = true; 
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

    m_username = username;
    m_receiver = std::thread(&Client::receiveThread, this);

    while (m_running) {
        outputChatPrefix(username);

        std::string text;
        if (!std::getline(std::cin, text)) break;

        if (!m_running) break;

        if (text.empty()) continue;

        if (text == "/users") {
            std::string usersMsg = "USERS\n";
            if (send(m_sock, usersMsg.c_str(), usersMsg.size(), 0) < 0) {
                std::cerr << "Failed to send /users message to socket " << m_sock << ": " << strerror(errno) << std::endl;
            }
            continue;
        }

        if (text == "/quit") {
            std::cout << COLOR_YELLOW << "QUITING\n" << COLOR_RESET;
            m_running = false;
            std::string quitMsg = "QUIT\n";
            if (send(m_sock, quitMsg.c_str(), quitMsg.size(), 0) < 0) {
                std::cerr << "Failed to send /quit message to socket " << m_sock << ": " << strerror(errno) << std::endl;
            }
            break;
        }

        if (text.rfind("/kick ", 0) == 0) {
            std::string targetUser = text.substr(6);
            if (targetUser.empty()) {
                std::cout << COLOR_RED << "Specify a user to kick!\n" << COLOR_RESET;
                continue;
            }

            std::string kickMsg = "KICK|" + targetUser + "\n";
            if (send(m_sock, kickMsg.c_str(), kickMsg.size(), 0) < 0) {
                std::cerr << "Failed to send kick command\n";
            }
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
            if (send(m_sock, msg.c_str(), msg.size(), 0) < 0) {
                std::cerr << "Failed to send private message\n";
            }
            continue;
        }

        std::string msg = "MSG|" + text + "\n";
        if (send(m_sock, msg.c_str(), msg.size(), 0) < 0) {
            std::cerr << "Failed to send to socket " << m_sock << ": " << strerror(errno) << std::endl;
        }
    }

    m_running = false;
    shutdown(m_sock, SHUT_RDWR);
    m_receiver.join();

    std::cout << CURSOR_LINE_START << CLEAR_LINE; 
    std::cout << COLOR_GREEN << "Goodbye!\n" << COLOR_RESET;
    return 0;
}