#include "nng_http_client.h"
#include <string>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <iostream>

int main() {
    try {
        NngHttpClient client;
        if (!client.connect("http://localhost:8080")) {
            std::cerr << "Failed to connect" << std::endl;
            return 1;
        }
        
        // std::string login_req = "{\"username\":\"admin\",\"password\":\"admin\"}";
        // auto login_resp = client.post("/api/v1/auth/login", login_req);
        // std::cout << "Login response: " << login_resp.body << std::endl;

        HttpRequest post_req;
        post_req.path = "/api/v1/auth/login";
        post_req.body = "{\"username\":\"admin\",\"password\":\"admin\"}";
        post_req.content_type = "application/json";
        
        HttpResponse post_resp;
        if (client.post(post_req, post_resp)) {
            if (post_resp.is_success()) {
                std::cout << "POST Success: " << post_resp.status_code << std::endl;
                std::cout << "Response: " << post_resp.body << std::endl;
            } else {
                std::cerr << "POST HTTP Error: " << post_resp.error_message << std::endl;
            }
        } else {
            std::cerr << "POST Failed: " << post_resp.error_message << std::endl;
        }
        
        client.close();

    } catch (const std::exception& e) {
        std::cout << "Client initialization failed: " << e.what() << std::endl;
    }
    
    return 0;
}
