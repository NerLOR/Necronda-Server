/**
 * Necronda Web Server
 * FastCGI interface implementation
 * src/fastcgi.c
 * Lorenz Stechauner, 2020-12-26
 */

#include "fastcgi.h"
#include "necronda-server.h"

#include <sys/un.h>


char *fastcgi_add_param(char *buf, const char *key, const char *value) {
    char *ptr = buf;
    unsigned long key_len = strlen(key);
    unsigned long val_len = strlen(value);


    if (key_len <= 127) {
        ptr[0] = (char) (key_len & 0x7F);
        ptr++;
    } else {
        ptr[0] = (char) (0x80 | (key_len >> 24));
        ptr[1] = (char) ((key_len >> 16) & 0xFF);
        ptr[2] = (char) ((key_len >> 8) & 0xFF);
        ptr[3] = (char) (key_len & 0xFF);
        ptr += 4;
    }
    if (val_len <= 127) {
        ptr[0] = (char) (val_len & 0x7F);
        ptr++;
    } else {
        ptr[0] = (char) (0x80 | (val_len >> 24));
        ptr[1] = (char) ((val_len >> 16) & 0xFF);
        ptr[2] = (char) ((val_len >> 8) & 0xFF);
        ptr[3] = (char) (val_len & 0xFF);
        ptr += 4;
    }

    memcpy(ptr, key, key_len);
    ptr += key_len;
    memcpy(ptr, value, val_len);
    ptr += val_len;

    return ptr;
}

int fastcgi_init(fastcgi_conn *conn, unsigned int client_num, unsigned int req_num, const sock *client,
                 const http_req *req, const http_uri *uri) {
    unsigned short req_id = (client_num & 0xFFF) << 4;
    if (client_num == 0) {
        req_id |= (req_num + 1) & 0xF;
    } else {
        req_id |= req_num & 0xF;
    }
    conn->req_id = req_id;
    conn->out_buf = NULL;
    conn->out_off = 0;

    int php_fpm = socket(AF_UNIX, SOCK_STREAM, 0);
    if (php_fpm < 0) {
        fprintf(stderr, ERR_STR "Unable to create unix socket: %s" CLR_STR "\n", strerror(errno));
        return -1;
    }
    conn->socket = php_fpm;

    struct sockaddr_un php_fpm_addr = {AF_UNIX, PHP_FPM_SOCKET};
    if (connect(conn->socket, (struct sockaddr *) &php_fpm_addr, sizeof(php_fpm_addr)) < 0) {
        fprintf(stderr, ERR_STR "Unable to connect to unix socket of PHP-FPM: %s" CLR_STR "\n", strerror(errno));
        return -1;
    }

    FCGI_Header header = {
            .version = FCGI_VERSION_1,
            .requestIdB1 = req_id >> 8,
            .requestIdB0 = req_id & 0xFF,
            .paddingLength = 0,
            .reserved = 0
    };

    header.type = FCGI_BEGIN_REQUEST;
    header.contentLengthB1 = 0;
    header.contentLengthB0 = sizeof(FCGI_BeginRequestBody);
    FCGI_BeginRequestRecord begin = {
            header,
            {.roleB1 = (FCGI_RESPONDER >> 8) & 0xFF, .roleB0 = FCGI_RESPONDER & 0xFF, .flags = 0}
    };
    if (send(conn->socket, &begin, sizeof(begin), 0) != sizeof(begin)) {
        fprintf(stderr, ERR_STR "Unable to send to PHP-FPM: %s" CLR_STR "\n", strerror(errno));
        return -2;
    }

    char param_buf[4096];
    char buf0[256];
    char *param_ptr = param_buf + sizeof(header);

    param_ptr = fastcgi_add_param(param_ptr, "REDIRECT_STATUS", "CGI");
    param_ptr = fastcgi_add_param(param_ptr, "DOCUMENT_ROOT", uri->webroot);
    param_ptr = fastcgi_add_param(param_ptr, "GATEWAY_INTERFACE", "CGI/1.1");
    param_ptr = fastcgi_add_param(param_ptr, "SERVER_SOFTWARE", SERVER_STR);
    param_ptr = fastcgi_add_param(param_ptr, "SERVER_PROTOCOL", "HTTP/1.1");
    param_ptr = fastcgi_add_param(param_ptr, "SERVER_NAME", http_get_header_field(&req->hdr, "Host"));
    if (client->enc) {
        param_ptr = fastcgi_add_param(param_ptr, "HTTPS", "on");
    }

    struct sockaddr_storage addr_storage;
    struct sockaddr_in6 *addr;
    socklen_t len = sizeof(addr_storage);
    getsockname(client->socket, (struct sockaddr *) &addr_storage, &len);
    addr = (struct sockaddr_in6 *) &addr_storage;
    sprintf(buf0, "%i", addr->sin6_port);
    param_ptr = fastcgi_add_param(param_ptr, "SERVER_PORT", buf0);

    len = sizeof(addr_storage);
    getpeername(client->socket, (struct sockaddr *) &addr_storage, &len);
    addr = (struct sockaddr_in6 *) &addr_storage;
    sprintf(buf0, "%i", addr->sin6_port);
    param_ptr = fastcgi_add_param(param_ptr, "REMOTE_PORT", buf0);

    char addr_str[INET6_ADDRSTRLEN];
    char *addr_ptr;
    inet_ntop(addr->sin6_family, (void *) &addr->sin6_addr, addr_str, INET6_ADDRSTRLEN);
    if (strncmp(addr_str, "::ffff:", 7) == 0) {
        addr_ptr = addr_str + 7;
    } else {
        addr_ptr = addr_str;
    }
    param_ptr = fastcgi_add_param(param_ptr, "REMOTE_ADDR", addr_ptr);
    param_ptr = fastcgi_add_param(param_ptr, "REMOTE_HOST", addr_ptr);
    //param_ptr = fastcgi_add_param(param_ptr, "REMOTE_IDENT", "");
    //param_ptr = fastcgi_add_param(param_ptr, "REMOTE_USER", "");

    param_ptr = fastcgi_add_param(param_ptr, "REQUEST_METHOD", req->method);
    param_ptr = fastcgi_add_param(param_ptr, "REQUEST_URI", req->uri);
    param_ptr = fastcgi_add_param(param_ptr, "SCRIPT_NAME", uri->filename + strlen(uri->webroot));
    param_ptr = fastcgi_add_param(param_ptr, "SCRIPT_FILENAME", uri->filename);
    //param_ptr = fastcgi_add_param(param_ptr, "PATH_TRANSLATED", uri->filename);

    param_ptr = fastcgi_add_param(param_ptr, "QUERY_STRING", uri->query != NULL ? uri->query : "");
    if (uri->pathinfo != NULL && strlen(uri->pathinfo) > 0) {
        sprintf(buf0, "/%s", uri->pathinfo);
    } else {
        sprintf(buf0, "");
    }
    param_ptr = fastcgi_add_param(param_ptr, "PATH_INFO", buf0);

    //param_ptr = fastcgi_add_param(param_ptr, "AUTH_TYPE", "");
    char *content_length = http_get_header_field(&req->hdr, "Content-Length");
    param_ptr = fastcgi_add_param(param_ptr, "CONTENT_LENGTH", content_length != NULL ? content_length : "");
    char *content_type = http_get_header_field(&req->hdr, "Content-Type");
    param_ptr = fastcgi_add_param(param_ptr, "CONTENT_TYPE", content_type != NULL ? content_type : "");

    for (int i = 0; i < req->hdr.field_num; i++) {
        char *ptr = buf0;
        ptr += sprintf(ptr, "HTTP_");
        for (int j = 0; j < strlen(req->hdr.fields[i][0]); j++, ptr++) {
            char ch = req->hdr.fields[i][0][j];
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
                ch = ch;
            } else if (ch >= 'a' && ch <= 'z') {
                ch &= 0x5F;
            } else {
                ch = '_';
            }
            ptr[0] = ch;
            ptr[1] = 0;
        }
        param_ptr = fastcgi_add_param(param_ptr, buf0, req->hdr.fields[i][1]);
    }

    unsigned short param_len = param_ptr - param_buf - sizeof(header);
    header.type = FCGI_PARAMS;
    header.contentLengthB1 = param_len >> 8;
    header.contentLengthB0 = param_len & 0xFF;
    memcpy(param_buf, &header, sizeof(header));
    if (send(conn->socket, param_buf, param_len + sizeof(header), 0) != param_len + sizeof(header)) {
        fprintf(stderr, ERR_STR "Unable to send to PHP-FPM: %s" CLR_STR "\n", strerror(errno));
        return -2;
    }

    header.type = FCGI_PARAMS;
    header.contentLengthB1 = 0;
    header.contentLengthB0 = 0;
    if (send(conn->socket, &header, sizeof(header), 0) != sizeof(header)) {
        fprintf(stderr, ERR_STR "Unable to send to PHP-FPM: %s" CLR_STR "\n", strerror(errno));
        return -2;
    }

    return 0;
}

int fastcgi_close_stdin(fastcgi_conn *conn) {
    FCGI_Header header = {
            .version = FCGI_VERSION_1,
            .type = FCGI_STDIN,
            .requestIdB1 = conn->req_id >> 8,
            .requestIdB0 = conn->req_id & 0xFF,
            .contentLengthB1 = 0,
            .contentLengthB0 = 0,
            .paddingLength = 0,
            .reserved = 0
    };

    if (send(conn->socket, &header, sizeof(header), 0) != sizeof(header)) {
        fprintf(stderr, ERR_STR "Unable to send to PHP-FPM: %s" CLR_STR "\n", strerror(errno));
        return -2;
    }

    return 0;
}

int fastcgi_php_error(char *msg, int msg_len, char *err_msg) {
    char *msg_str = malloc(msg_len + 1);
    char *ptr0 = msg_str;
    strncpy(msg_str, msg, msg_len);
    char *ptr1 = NULL;
    int len;
    int err = 0;
    while (1) {
        ptr1 = strstr(ptr0, "PHP message: ");
        if (ptr1 == NULL) {
            len = (int) (msg_len - (ptr0 - msg_str));
        } else {
            len = (int) (ptr1 - ptr0);
        }
        if (len == 0) {
            goto next;
        }

        int msg_type = 0;
        int msg_pre_len = 0;
        if (len >= 14 && strncmp(ptr0, "PHP Warning:  ", 14) == 0) {
            msg_type = 1;
            msg_pre_len = 14;
        } else if (len >= 18 && strncmp(ptr0, "PHP Fatal error:  ", 18) == 0) {
            msg_type = 2;
            msg_pre_len = 18;
        } else if (len >= 18 && strncmp(ptr0, "PHP Parse error:  ", 18) == 0) {
            msg_type = 2;
            msg_pre_len = 18;
        } else if (len >= 18 && strncmp(ptr0, "PHP Notice:  ", 13) == 0) {
            msg_type = 1;
            msg_pre_len = 13;
        }

        char *ptr2 = ptr0;
        char *ptr3;
        int len2;
        while (ptr2 - ptr0 < len) {
            ptr3 = strchr(ptr2, '\n');
            len2 = (int) (len - (ptr2 - ptr0));
            if (ptr3 != NULL && (ptr3 - ptr2) < len2) {
                len2 = (int) (ptr3 - ptr2);
            }
            print("%s%.*s%s", msg_type == 1 ? WRN_STR : msg_type == 2 ? ERR_STR: "", len2, ptr2, msg_type != 0 ? CLR_STR : "");
            if (msg_type == 2 && ptr2 == ptr0) {
                sprintf(err_msg, "%.*s", len2, ptr2);
                err = 1;
            }
            if (ptr3 == NULL) {
                break;
            }
            ptr2 = ptr3 + 1;
        }

        next:
        if (ptr1 == NULL) {
            break;
        }
        ptr0 = ptr1 + 13;
    }
    return err;
}

int fastcgi_header(fastcgi_conn *conn, http_res *res, char *err_msg) {
    FCGI_Header header;
    char *content;
    unsigned short content_len, req_id;
    int ret;
    int err = 0;

    while (1) {
        ret = recv(conn->socket, &header, sizeof(header), 0);
        if (ret < 0) {
            res->status = http_get_status(502);
            sprintf(err_msg, "Unable to communicate with PHP-FPM.");
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM: %s" CLR_STR "\n", strerror(errno));
            return -1;
        } else if (ret != sizeof(header)) {
            res->status = http_get_status(502);
            sprintf(err_msg, "Unable to communicate with PHP-FPM.");
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM" CLR_STR "\n");
            return -1;
        }
        req_id = (header.requestIdB1 << 8) | header.requestIdB0;
        content_len = (header.contentLengthB1 << 8) | header.contentLengthB0;
        content = malloc(content_len + header.paddingLength);
        ret = recv(conn->socket, content, content_len + header.paddingLength, 0);
        if (ret < 0) {
            res->status = http_get_status(502);
            sprintf(err_msg, "Unable to communicate with PHP-FPM.");
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM: %s" CLR_STR "\n", strerror(errno));
            free(content);
            return -1;
        } else if (ret != (content_len + header.paddingLength)) {
            res->status = http_get_status(502);
            sprintf(err_msg, "Unable to communicate with PHP-FPM.");
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM" CLR_STR "\n");
            free(content);
            return -1;
        }

        if (req_id != conn->req_id) {
            continue;
        }

        if (header.type == FCGI_END_REQUEST) {
            FCGI_EndRequestBody *body = (FCGI_EndRequestBody *) content;
            int app_status = (body->appStatusB3 << 24) | (body->appStatusB2 << 16) | (body->appStatusB1 << 8) |
                             body->appStatusB0;
            if (body->protocolStatus != FCGI_REQUEST_COMPLETE) {
                print(ERR_STR "FastCGI protocol error: %i" CLR_STR, body->protocolStatus);
            }
            if (app_status != 0) {
                print(ERR_STR "Script terminated with exit code %i" CLR_STR, app_status);
            }
            close(conn->socket);
            conn->socket = 0;
            free(content);
            return -2;
        } else if (header.type == FCGI_STDERR) {
            err = err || fastcgi_php_error(content, content_len, err_msg);
        } else if (header.type == FCGI_STDOUT) {
            break;
        } else {
            fprintf(stderr, ERR_STR "Unknown FastCGI type: %i" CLR_STR "\n", header.type);
        }

        free(content);
    }
    if (err) {
        res->status = http_get_status(500);
        return -3;
    }

    conn->out_buf = content;
    conn->out_len = content_len;
    conn->out_off = (unsigned short) (strstr(content, "\r\n\r\n") - content + 4);

    // TODO process headers and add fields to res

    return 0;
}

int fastcgi_send(fastcgi_conn *conn, sock *client, int flags) {
    FCGI_Header header;
    int ret;
    char buf0[256];
    int len;
    char *content, *ptr;
    unsigned short req_id, content_len;
    char comp_out[4096];
    int finish_comp = 0;

    z_stream strm;
    if (flags & FASTCGI_COMPRESS) {
        int level = NECRONDA_ZLIB_LEVEL;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        if (deflateInit(&strm, level) != Z_OK) {
            fprintf(stderr, ERR_STR "Unable to init deflate: %s" CLR_STR "\n", strerror(errno));
            flags &= !FASTCGI_COMPRESS;
        }
    }

    if (conn->out_buf != NULL && conn->out_len > conn->out_off) {
        content = conn->out_buf;
        ptr = content + conn->out_off;
        content_len = conn->out_len - conn->out_off;
        goto out;
    }

    while (1) {
        ret = recv(conn->socket, &header, sizeof(header), 0);
        if (ret < 0) {
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM: %s" CLR_STR "\n", strerror(errno));
            return -1;
        } else if (ret != sizeof(header)) {
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM" CLR_STR "\n");
            return -1;
        }
        req_id = (header.requestIdB1 << 8) | header.requestIdB0;
        content_len = (header.contentLengthB1 << 8) | header.contentLengthB0;
        content = malloc(content_len + header.paddingLength);
        ptr = content;
        ret = recv(conn->socket, content, content_len + header.paddingLength, 0);
        if (ret < 0) {
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM: %s" CLR_STR "\n", strerror(errno));
            free(content);
            return -1;
        } else if (ret != (content_len + header.paddingLength)) {
            fprintf(stderr, ERR_STR "Unable to receive from PHP-FPM" CLR_STR "\n");
            free(content);
            return -1;
        }

        if (header.type == FCGI_END_REQUEST) {
            FCGI_EndRequestBody *body = (FCGI_EndRequestBody *) content;
            int app_status = (body->appStatusB3 << 24) | (body->appStatusB2 << 16) | (body->appStatusB1 << 8) |
                             body->appStatusB0;
            if (body->protocolStatus != FCGI_REQUEST_COMPLETE) {
                print(ERR_STR "FastCGI protocol error: %i" CLR_STR, body->protocolStatus);
            }
            if (app_status != 0) {
                print(ERR_STR "Script terminated with exit code %i" CLR_STR, app_status);
            }
            close(conn->socket);
            conn->socket = 0;
            free(content);

            if (flags & FASTCGI_COMPRESS) {
                finish_comp = 1;
                content_len = 0;
                goto out;
                finish:
                deflateEnd(&strm);
            }

            if (flags & FASTCGI_CHUNKED) {
                if (client->enc) {
                    SSL_write(client->ssl, "0\r\n\r\n", 5);
                } else {
                    send(client->socket, "0\r\n\r\n", 5, 0);
                }
            }

            return 0;
        } else if (header.type == FCGI_STDERR) {
            print(ERR_STR "%.*s" CLR_STR, content_len, content);
        } else if (header.type == FCGI_STDOUT) {
            out:
            if (flags & FASTCGI_COMPRESS) {
                strm.avail_in = content_len;
                strm.next_in = (unsigned char *) ptr;
            }
            do {
                int buf_len = content_len;
                if (flags & FASTCGI_COMPRESS) {
                    strm.avail_out = sizeof(comp_out);
                    strm.next_out = (unsigned char *) comp_out;
                    deflate(&strm, finish_comp ? Z_FINISH : Z_NO_FLUSH);
                    strm.avail_in = 0;
                    ptr = comp_out;
                    buf_len = (int) (sizeof(comp_out) - strm.avail_out);
                }
                if (buf_len != 0) {
                    len = sprintf(buf0, "%X\r\n", buf_len);
                    if (client->enc) {
                        if (flags & FASTCGI_CHUNKED) SSL_write(client->ssl, buf0, len);
                        SSL_write(client->ssl, ptr, buf_len);
                        if (flags & FASTCGI_CHUNKED) SSL_write(client->ssl, "\r\n", 2);
                    } else {
                        if (flags & FASTCGI_CHUNKED) send(client->socket, buf0, len, 0);
                        send(client->socket, ptr, buf_len, 0);
                        if (flags & FASTCGI_CHUNKED) send(client->socket, "\r\n", 2, 0);
                    }
                }
            } while ((flags & FASTCGI_COMPRESS) && strm.avail_out == 0);
            if (finish_comp) goto finish;
        } else {
            fprintf(stderr, ERR_STR "Unknown FastCGI type: %i" CLR_STR "\n", header.type);
        }
        free(content);
    }
}

int fastcgi_receive(fastcgi_conn *conn, sock *client, unsigned long len) {
    unsigned long rcv_len = 0;
    char *buf[16384];
    int ret;
    FCGI_Header header = {
            .version = FCGI_VERSION_1,
            .type = FCGI_STDIN,
            .requestIdB1 = conn->req_id >> 8,
            .requestIdB0 = conn->req_id & 0xFF,
            .contentLengthB1 = 0,
            .contentLengthB0 = 0,
            .paddingLength = 0,
            .reserved = 0
    };
    while (rcv_len < len) {
        if (client->enc) {
            ret = SSL_read(client->ssl, buf, sizeof(buf));
            if (ret <= 0) {
                print(ERR_STR "Unable to receive: %s" CLR_STR, ssl_get_error(client->ssl, rcv_len));
                return -1;
            }
        } else {
            ret = recv(client->socket, buf, sizeof(buf), 0);
            if (ret <= 0) {
                print(ERR_STR "Unable to receive: %s" CLR_STR, strerror(errno));
                return -1;
            }
        }
        rcv_len += ret;
        header.contentLengthB1 = (ret >> 8) & 0xFF;
        header.contentLengthB0 = ret & 0xFF;
        if (send(conn->socket, &header, sizeof(header), 0) != sizeof(header)) goto err;
        if (send(conn->socket, buf, ret, 0) != ret) {
            err:
            fprintf(stderr, ERR_STR "Unable to send to PHP-FPM: %s" CLR_STR "\n", strerror(errno));
            return -2;
        }
    }
    return 0;
}
