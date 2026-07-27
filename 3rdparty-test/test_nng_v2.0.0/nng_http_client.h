// nng_http_client.h
#ifndef NNG_HTTP_CLIENT_H
#define NNG_HTTP_CLIENT_H

#include <string>
#include <map>
#include <memory>
#include <nng/nng.h>
#include <nng/http.h>

// HTTP响应结构体
struct HttpResponse {
    int status_code = 0;           // HTTP状态码，如200、404等
    std::string body;              // 响应体内容
    std::string error_message;     // 错误信息（成功时为空）
    std::map<std::string, std::string> headers;  // 响应头（可选）
    
    void clear() {
        status_code = 0;
        body.clear();
        error_message.clear();
        headers.clear();
    }
    
    bool is_success() const {
        return status_code >= 200 && status_code < 300 && error_message.empty();
    }
};

// HTTP请求结构体
struct HttpRequest {
    std::string path;              // 请求路径，如 "/api/users"
    std::string body;              // 请求体（POST/PUT使用）
    std::map<std::string, std::string> headers;  // 自定义请求头
    int timeout_ms = 30000;        // 超时时间（毫秒）
    
    // POST请求专用
    std::string content_type = "application/json";  // Content-Type
};

class NngHttpClient {
public:
    NngHttpClient();
    ~NngHttpClient();

    // 初始化并连接到服务器
    bool connect(const std::string& url);
    
    // GET请求（response作为输出参数）
    bool get(const HttpRequest& request, HttpResponse& response);
    
    // POST请求（response作为输出参数）
    bool post(const HttpRequest& request, HttpResponse& response);
    
    // PUT请求（response作为输出参数）
    bool put(const HttpRequest& request, HttpResponse& response);
    
    // DELETE请求（response作为输出参数）
    bool del(const HttpRequest& request, HttpResponse& response);
    
    // 关闭连接
    void close();
    
    // 检查是否已连接
    bool is_connected() const;

private:
    // 通用请求方法
    bool doRequest(const std::string& method, const HttpRequest& request, HttpResponse& response);
    
    // 设置请求头
    void setupRequestHeaders(nng_http* conn, const HttpRequest& request, 
                             const std::string& method, const std::string& body);
    
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

#endif