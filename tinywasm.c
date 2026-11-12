#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSIZE 1024

// Structure to hold parsing results to send back to JavaScript
typedef struct {
    char method[16];
    char uri[256];
    char version[16];
    char filetype[32];
    int is_static;
    int status_code;
} HttpResponseInfo;

// Emscripten export macro
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE
void parse_http_request(const char* raw_request, HttpResponseInfo* out_info) {
    char method[BUFSIZE], uri[BUFSIZE], version[BUFSIZE];
    
    // Parse the request line
    if (sscanf(raw_request, "%s %s %s", method, uri, version) != 3) {
        out_info->status_code = 400;
        return;
    }

    strcpy(out_info->method, method);
    strcpy(out_info->uri, uri);
    strcpy(out_info->version, version);

    // Check method (Only GET supported)
    if (strcasecmp(method, "GET") != 0) {
        out_info->status_code = 501; // Not Implemented
        return;
    }

    // Determine if static or dynamic content
    if (!strstr(uri, "cgi-bin")) {
        out_info->is_static = 1;
        
        // Determine file type
        if (strstr(uri, ".html") || strcmp(uri, "/") == 0) {
            strcpy(out_info->filetype, "text/html");
        } else if (strstr(uri, ".gif")) {
            strcpy(out_info->filetype, "image/gif");
        } else if (strstr(uri, ".jpg")) {
            strcpy(out_info->filetype, "image/jpg");
        } else {
            strcpy(out_info->filetype, "text/plain");
        }
    } else {
        out_info->is_static = 0;
        strcpy(out_info->filetype, "text/plain");
    }

    out_info->status_code = 200; // OK
}