#include "Client.hpp"

#include <cstdint>
#include <string>

constexpr uint16_t PORT = 54000;
constexpr const char* SERVER_IP = "127.0.0.1";

int main(int argc, char* argv[]) {
    std::string serverIp = (argc > 1) ? argv[1] : SERVER_IP;

    Client client(serverIp, PORT);
    return client.run();
}