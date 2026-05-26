#pragma once

#include <nng/nng.h>
#include <nng/http.h>
#include <string>

namespace kungfu::service {

class ApiGateway;

// NNG HTTP handler callback - registered as tree handler for /api/v1/
void api_http_handler(nng_http* conn, void* arg, nng_aio* aio);

// Helper: send JSON response
void send_json_response(nng_http* conn, nng_aio* aio, int status, const std::string& body);

// Helper: send error response
void send_error(nng_http* conn, nng_aio* aio, int status, const std::string& message);

// Helper: extract path parameter from URI (e.g., "/api/v1/orders/123" -> "123")
std::string extract_path_param(const std::string& uri, const std::string& prefix);

// Helper: extract Bearer token from Authorization header
std::string extract_bearer_token(nng_http* conn);

} // namespace kungfu::service
