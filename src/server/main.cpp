#include "Server.hpp"

#include <iostream>

constexpr uint16_t PORT = 54000;

int main(int argc, char* argv[]) {
    try {
        Server server(PORT);
        server.acceptClients();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}