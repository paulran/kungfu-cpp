#include "socket.h"
#include <iostream>
#include <unistd.h>

int main()
{
    try {
        // Create SUBSCRIBE socket
        socket s(protocol::SUBSCRIBE);
        
        // Subscribe to all messages (empty topic)
        const char* empty_topic = "";
        s.setsockopt(NN_SUB, NN_SUB_SUBSCRIBE, empty_topic, strlen(empty_topic));
        
        // Connect to server
        std::string path = "/tmp/test_pub";
        s.connect(path);
        std::cout << "Client connected to: " << s.get_url() << std::endl;
        
        // Receive messages
        int count = 0;
        while (count < 10) {
            int rc = s.recv();
            if (rc > 0) {
                std::cout << "Received: " << s.last_message() << ", length=" << rc << std::endl;
                count++;
            } else {
                // No message yet, wait a bit
                usleep(100000);
            }
        }
        
        std::cout << "Client done." << std::endl;
    } catch (const nn_exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
