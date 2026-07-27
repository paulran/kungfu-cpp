#include "socket.h"
#include <iostream>
#include <unistd.h>

int main()
{
    try {
        // Create PUBLISH socket
        socket s(protocol::PUBLISH);
        
        // Bind to IPC path
        std::string path = "/tmp/test_pub";
        s.bind(path);
        std::cout << "Server bound to: " << s.get_url() << std::endl;
        
        // Publish messages in a loop
        for (int i = 0; i < 10; ++i) {
            std::string msg = "Message " + std::to_string(i);
            int rc = s.send(msg);
            std::cout << "Sent: " << msg << ", rc=" << rc << std::endl;
            sleep(1);
        }
        
        std::cout << "Server done." << std::endl;
    } catch (const nn_exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
