// nng_http_client.cpp
#include "nng_http_client.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

static inline const char* nng_strerror_safe(int err) {
    return nng_strerror((nng_err)err);
}

struct NngHttpClient::Impl {
    nng_http_client* client = nullptr;
    nng_http* conn = nullptr;
    nng_url* url = nullptr;
    nng_aio* aio = nullptr;
    bool connected = false;
    std::string base_url;
    
    ~Impl() {
        if (aio) nng_aio_free(aio);
        // if (conn) nng_http_conn_free(conn);
        if (client) nng_http_client_free(client);
        if (url) nng_url_free(url);
    }
};

NngHttpClient::NngHttpClient() : pImpl(std::make_unique<Impl>()) {}

NngHttpClient::~NngHttpClient() = default;

bool NngHttpClient::connect(const std::string& url_str) {
    int rv;
    
    // 初始化NNG
    if ((rv = nng_init(nullptr)) != 0) {
        std::cerr << "nng_init failed: " << nng_strerror_safe(rv) << std::endl;
        return false;
    }
    
    // 分配AIO
    if ((rv = nng_aio_alloc(&pImpl->aio, nullptr, nullptr)) != 0) {
        std::cerr << "nng_aio_alloc failed: " << nng_strerror_safe(rv) << std::endl;
        return false;
    }
    
    // 解析URL
    if ((rv = nng_url_parse(&pImpl->url, url_str.c_str())) != 0) {
        std::cerr << "nng_url_parse failed: " << nng_strerror_safe(rv) << std::endl;
        return false;
    }
    
    // 创建HTTP客户端
    if ((rv = nng_http_client_alloc(&pImpl->client, pImpl->url)) != 0) {
        std::cerr << "nng_http_client_alloc failed: " << nng_strerror_safe(rv) << std::endl;
        return false;
    }
    
    // 开始连接（同步等待）
    nng_http_client_connect(pImpl->client, pImpl->aio);
    nng_aio_wait(pImpl->aio);
    
    if ((rv = nng_aio_result(pImpl->aio)) != 0) {
        std::cerr << "Connection failed: " << nng_strerror_safe(rv) << std::endl;
        return false;
    }
    
    // 获取连接对象
    pImpl->conn = (nng_http_conn*)nng_aio_get_output(pImpl->aio, 0);
    pImpl->connected = true;
    pImpl->base_url = url_str;
    
    return true;
}

void NngHttpClient::setupRequestHeaders(nng_http* conn, const HttpRequest& request,
                                         const std::string& method, const std::string& body) {
    // // 设置Host头（通常自动设置）
    // if (pImpl->url && pImpl->url->u_host) {
    //     nng_http_set_header(conn, "Host", pImpl->url->u_host);
    // }
    
    // 设置Connection头
    nng_http_set_header(conn, "Connection", "keep-alive");
    
    // 设置Content-Type（POST/PUT时）
    if (method == "POST" || method == "PUT") {
        if (!request.content_type.empty()) {
            nng_http_set_header(conn, "Content-Type", request.content_type.c_str());
        }
        
        // 设置Content-Length
        char len_str[32];
        snprintf(len_str, sizeof(len_str), "%zu", body.size());
        nng_http_set_header(conn, "Content-Length", len_str);
    }
    
    // 设置自定义请求头
    for (const auto& [key, value] : request.headers) {
        nng_http_set_header(conn, key.c_str(), value.c_str());
    }
    if (!token_.empty()) {
        std::string auth = "Bearer " + token_;
        nng_http_set_header(conn, "Authorization", auth.c_str());
    }
}

bool NngHttpClient::doRequest(const std::string& method, const HttpRequest& request, HttpResponse& response) {
    // 清空响应对象
    response.clear();
    
    if (!pImpl->connected) {
        response.error_message = "Not connected to server";
        return false;
    }
    
    int rv;
    
    nng_http_set_method(pImpl->conn, method.c_str());

    // 设置请求URI（如果路径不为空）
    if (!request.path.empty()) {
        // 注意：需要根据实际API调整，某些版本可能需要重新创建连接
        nng_http_set_uri(pImpl->conn, request.path.c_str(), nullptr);
    }
    
    // 设置请求头
    setupRequestHeaders(pImpl->conn, request, method, request.body);
    
    // 对于POST/PUT，需要先写入请求体（这里简化处理）
    // 实际实现可能需要使用nng_http_write_request_body
    if (request.body.size() > 0) {
        nng_http_copy_body(pImpl->conn, (void*)(request.body.c_str()), request.body.size());
    }
    
    // 发送请求
    nng_http_write_request(pImpl->conn, pImpl->aio);
    nng_aio_wait(pImpl->aio);
    
    if ((rv = nng_aio_result(pImpl->aio)) != 0) {
        response.error_message = nng_strerror_safe(rv);
        return false;
    }
    
    // 读取响应头
    nng_http_read_response(pImpl->conn, pImpl->aio);
    nng_aio_wait(pImpl->aio);
    
    if ((rv = nng_aio_result(pImpl->aio)) != 0) {
        response.error_message = nng_strerror_safe(rv);
        return false;
    }
    
    // 获取响应状态
    response.status_code = nng_http_get_status(pImpl->conn);
    const char* reason = nng_http_get_reason(pImpl->conn);
    
    if (response.status_code != NNG_HTTP_STATUS_OK && 
        response.status_code != NNG_HTTP_STATUS_CREATED &&
        response.status_code != NNG_HTTP_STATUS_ACCEPTED) {
        response.error_message = std::string("HTTP Error: ") + reason;
        // 即使出错，也继续尝试读取错误响应体
    }
    
    // 读取响应头（可选，这里简单示例）
    // 遍历所有响应头需要根据实际API支持
    
    // 获取Content-Length
    const char* hdr = nng_http_get_header(pImpl->conn, "Content-Length");
    if (hdr == nullptr) {
        // 没有Content-Length，可能使用chunked encoding
        // 官方示例不支持，返回空body
        return true;  // 请求成功但无响应体
    }
    
    int len = atoi(hdr);
    if (len == 0) {
        return true;  // 无响应体
    }
    
    // 分配缓冲区并读取body
    std::string body;
    body.resize(len);
    
    nng_iov iov;
    iov.iov_len = len;
    iov.iov_buf = &body[0];
    
    nng_aio_set_iov(pImpl->aio, 1, &iov);
    nng_http_read_all(pImpl->conn, pImpl->aio);
    nng_aio_wait(pImpl->aio);
    
    if ((rv = nng_aio_result(pImpl->aio)) != 0) {
        response.error_message = nng_strerror_safe(rv);
        return false;
    }
    
    response.body = std::move(body);
    return true;
}

bool NngHttpClient::get(const HttpRequest& request, HttpResponse& response) {
    return doRequest("GET", request, response);
}

bool NngHttpClient::post(const HttpRequest& request, HttpResponse& response) {
    return doRequest("POST", request, response);
}

bool NngHttpClient::put(const HttpRequest& request, HttpResponse& response) {
    return doRequest("PUT", request, response);
}

bool NngHttpClient::del(const HttpRequest& request, HttpResponse& response) {
    return doRequest("DELETE", request, response);
}

void NngHttpClient::close() {
    pImpl->connected = false;
    pImpl.reset(new Impl());
}

bool NngHttpClient::is_connected() const {
    return pImpl->connected;
}