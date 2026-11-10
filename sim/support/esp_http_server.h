#pragma once
// Lume Nano simulator: types only; the companion HTTP server is not built (sim/stubs/CompanionStub.cpp).
#include <cstddef>
#include "esp_err.h"
#include "HTTPClient.h"
using httpd_handle_t = void*;
typedef enum { HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3, HTTP_PUT = 4, HTTP_OPTIONS = 6, HTTP_PATCH = 28 } httpd_method_t;
typedef enum { HTTPD_500_INTERNAL_SERVER_ERROR = 0, HTTPD_404_NOT_FOUND = 7 } httpd_err_code_t;
struct httpd_req_t {
    httpd_handle_t handle = nullptr;
    int method = HTTP_GET;
    const char uri[512] = {};
    size_t content_len = 0;
    void* aux = nullptr;
    void* user_ctx = nullptr;
    void* sess_ctx = nullptr;
};
inline void* httpd_get_global_user_ctx(httpd_handle_t) { return nullptr; }
inline esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
