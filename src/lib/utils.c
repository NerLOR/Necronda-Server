/**
 * Necronda Web Server
 * Utilities
 * src/lib/utils.c
 * Lorenz Stechauner, 2020-12-03
 */

#include "utils.h"
#include <string.h>
#include <stdlib.h>

char *log_prefix;

char *format_duration(unsigned long micros, char *buf) {
    if (micros < 10000) {
        sprintf(buf, "%.1f ms", (double) micros / 1000);
    } else if (micros < 1000000) {
        sprintf(buf, "%li ms", micros / 1000);
    } else if (micros < 60000000) {
        sprintf(buf, "%.1f s", (double) micros / 1000000);
    } else if (micros < 6000000000) {
        sprintf(buf, "%.1f min", (double) micros / 1000000 / 60);
    } else {
        sprintf(buf, "%li min", micros / 1000000 / 60);
    }
    return buf;
}

int url_encode_component(const char *str, char *enc, ssize_t *size) {
    char *ptr = enc;
    char ch;
    memset(enc, 0, *size);
    for (int i = 0; i < strlen(str); i++, ptr++) {
        if ((ptr - enc) >= *size) {
            return -1;
        }
        ch = str[i];
        if (ch == ':' || ch == '/' || ch == '?' || ch == '#' || ch == '[' || ch == ']' || ch == '@' || ch == '!' ||
                ch == '$' || ch == '&' || ch == '\'' || ch == '(' || ch == ')' || ch == '*' || ch == '+' || ch == ',' ||
                ch == ';' || ch == '=' || ch < ' ' || ch > '~') {
            if ((ptr - enc + 2) >= *size) {
                return -1;
            }
            sprintf(ptr, "%%%02X", ch);
            ptr += 2;
        } else if (ch == ' ') {
            ptr[0] = '+';
        } else {
            ptr[0] = ch;
        }
    }
    *size = ptr - enc;
    return 0;
}

int url_encode(const char *str, char *enc, ssize_t *size) {
    char *ptr = enc;
    unsigned char ch;
    memset(enc, 0, *size);
    for (int i = 0; i < strlen(str); i++, ptr++) {
        if ((ptr - enc) >= *size) {
            return -1;
        }
        ch = str[i];
        if (ch > 0x7F || ch == ' ') {
            if ((ptr - enc + 2) >= *size) {
                return -1;
            }
            sprintf(ptr, "%%%02X", ch);
            ptr += 2;
        } else {
            ptr[0] = (char) ch;
        }
    }
    *size = ptr - enc;
    return 0;
}

int url_decode(const char *str, char *dec, ssize_t *size) {
    char *ptr = dec;
    char ch, buf[3];
    memset(dec, 0, *size);
    for (int i = 0; i < strlen(str); i++, ptr++) {
        if ((ptr - dec) >= *size) {
            return -1;
        }
        ch = str[i];
        if (ch == '+') {
            ch = ' ';
        } else if (ch == '%') {
            memcpy(buf, str + i + 1, 2);
            buf[2] = 0;
            ch = (char) strtol(buf, NULL, 16);
            i += 2;
        } else if (ch == '?') {
            strcpy(ptr, str + i);
            break;
        }
        ptr[0] = ch;
    }
    *size = ptr - dec;
    return 0;
}

int mime_is_compressible(const char *type) {
    return
        strncmp(type, "text/", 5) == 0 ||
        strncmp(type, "message/", 7) == 0 ||
        strstr(type, "+xml") != NULL ||
        strcmp(type, "application/javascript") == 0 ||
        strcmp(type, "application/json") == 0 ||
        strcmp(type, "application/xml") == 0 ||
        strcmp(type, "application/x-www-form-urlencoded") == 0 ||
        strcmp(type, "application/x-tex") == 0 ||
        strcmp(type, "application/x-httpd-php") == 0 ||
        strcmp(type, "application/x-latex") == 0;
}

MMDB_entry_data_list_s *mmdb_json(MMDB_entry_data_list_s *list, char *str, long *str_off, long str_len) {
    switch (list->entry_data.type) {
        case MMDB_DATA_TYPE_MAP:
            *str_off += sprintf(str + *str_off, "{");
            break;
        case MMDB_DATA_TYPE_ARRAY:
            *str_off += sprintf(str + *str_off, "[");
            break;
        case MMDB_DATA_TYPE_UTF8_STRING:
            *str_off += sprintf(str + *str_off, "\"%.*s\"", list->entry_data.data_size, list->entry_data.utf8_string);
            break;
        case MMDB_DATA_TYPE_UINT16:
            *str_off += sprintf(str + *str_off, "%u", list->entry_data.uint16);
            break;
        case MMDB_DATA_TYPE_UINT32:
            *str_off += sprintf(str + *str_off, "%u", list->entry_data.uint32);
            break;
        case MMDB_DATA_TYPE_UINT64:
            *str_off += sprintf(str + *str_off, "%lu", list->entry_data.uint64);
            break;
        case MMDB_DATA_TYPE_UINT128:
            *str_off += sprintf(str + *str_off, "%llu", (unsigned long long) list->entry_data.uint128);
            break;
        case MMDB_DATA_TYPE_INT32:
            *str_off += sprintf(str + *str_off, "%i", list->entry_data.uint32);
            break;
        case MMDB_DATA_TYPE_BOOLEAN:
            *str_off += sprintf(str + *str_off, "%s", list->entry_data.boolean ? "true" : "false");
            break;
        case MMDB_DATA_TYPE_FLOAT:
            *str_off += sprintf(str + *str_off, "%f", list->entry_data.float_value);
            break;
        case MMDB_DATA_TYPE_DOUBLE:
            *str_off += sprintf(str + *str_off, "%f", list->entry_data.double_value);
            break;
    }
    if (list->entry_data.type != MMDB_DATA_TYPE_MAP && list->entry_data.type != MMDB_DATA_TYPE_ARRAY) {
        return list->next;
    }
    MMDB_entry_data_list_s *next = list->next;
    int stat = 0;
    for (int i = 0; i < list->entry_data.data_size; i++) {
        next = mmdb_json(next, str, str_off, str_len);
        if (list->entry_data.type == MMDB_DATA_TYPE_MAP) {
            stat = !stat;
            if (stat) {
                i--;
                *str_off += sprintf(str + *str_off, ":");
                continue;
            }
        }
        if (i != list->entry_data.data_size - 1) *str_off += sprintf(str + *str_off, ",");
    }
    if (list->entry_data.type == MMDB_DATA_TYPE_MAP) {
        *str_off += sprintf(str + *str_off, "}");
    } else {
        *str_off += sprintf(str + *str_off, "]");
    }
    return next;
}
