/*
 * TwitchAdBlock for iOS
 *
 * Native iOS adaptation of pixeltris/TwitchAdSolutions video-swap-new 1.55.
 * The original project is MIT licensed; see LICENSE-TwitchAdSolutions.
 *
 * This intentionally uses the Objective-C runtime instead of private Twitch
 * symbols. That makes the interception points Foundation/AVFoundation APIs:
 * GraphQL requests are normalized through NSURLProtocol, and Twitch HLS is
 * loaded through an AVAssetResourceLoader delegate.
 */

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned long NSUInteger;
typedef long NSInteger;
#define TAS_INTERNAL_HEADER "X-TAS-Internal"
#define TAS_SCHEME "tashttps"
#define TAS_CLIENT_ID "kimne78kx3ncx6brgo4mv6wki5h1ko"
#define TAS_TOKEN_HASH "ed230aa1e33e07eebb8928504583da78a5173989fadfb1ac94be06a04f3cdbe9"

extern id objc_retain(id object);
extern void objc_release(id object);
extern void objc_setAssociatedObject(id object, const void *key, id value, uintptr_t policy);
extern uint32_t arc4random_uniform(uint32_t upper_bound);

static id msg0(id object, const char *selector) {
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static id msg1(id object, const char *selector, id a) {
    return ((id (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), a);
}

static id msg2(id object, const char *selector, id a, id b) {
    return ((id (*)(id, SEL, id, id))objc_msgSend)(object, sel_registerName(selector), a, b);
}

static void vmsg1(id object, const char *selector, id a) {
    ((void (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), a);
}

static void vmsg2(id object, const char *selector, id a, id b) {
    ((void (*)(id, SEL, id, id))objc_msgSend)(object, sel_registerName(selector), a, b);
}

static void vmsg3(id object, const char *selector, id a, id b, NSInteger c) {
    ((void (*)(id, SEL, id, id, NSInteger))objc_msgSend)(object, sel_registerName(selector), a, b, c);
}

static BOOL bmsg1(id object, const char *selector, id a) {
    return ((BOOL (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), a);
}

static NSInteger imsg0(id object, const char *selector) {
    return ((NSInteger (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static id nsstr(const char *value) {
    if (!value) return nil;
    return msg1((id)objc_getClass("NSString"), "stringWithUTF8String:", (id)value);
}

static const char *utf8(id value) {
    if (!value) return NULL;
    return ((const char *(*)(id, SEL))objc_msgSend)(value, sel_registerName("UTF8String"));
}

static id nsurl(const char *value) {
    return msg1((id)objc_getClass("NSURL"), "URLWithString:", nsstr(value));
}

static id data_from_bytes(const void *bytes, size_t length) {
    return ((id (*)(id, SEL, const void *, NSUInteger))objc_msgSend)(
        (id)objc_getClass("NSData"), sel_registerName("dataWithBytes:length:"), bytes, (NSUInteger)length);
}

static const uint8_t *data_bytes(id data) {
    return ((const uint8_t *(*)(id, SEL))objc_msgSend)(data, sel_registerName("bytes"));
}

static size_t data_length(id data) {
    return (size_t)((NSUInteger (*)(id, SEL))objc_msgSend)(data, sel_registerName("length"));
}

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_authorization[4096];
static char g_integrity[4096];
static char g_device_id[512];
static char g_channel[512];
static char g_master_url[8192];
static char g_backup_variant_url[8192];
static time_t g_backup_created;

static char g_association_key;
static IMP g_original_asset_init;
static IMP g_original_default_configuration;
static IMP g_original_ephemeral_configuration;
static Class g_protocol_class;
static Class g_loader_class;

static void copy_string(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = "";
    snprintf(destination, capacity, "%s", source);
}

static bool starts_with(const char *value, const char *prefix) {
    return value && prefix && strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool contains(const char *value, const char *needle) {
    return value && needle && strstr(value, needle) != NULL;
}

static char *copy_data_text(id data) {
    if (!data) return NULL;
    size_t length = data_length(data);
    char *result = calloc(length + 1, 1);
    if (!result) return NULL;
    memcpy(result, data_bytes(data), length);
    return result;
}

static void cache_header(id request, const char *header, char *destination, size_t capacity) {
    id value = msg1(request, "valueForHTTPHeaderField:", nsstr(header));
    const char *string = utf8(value);
    if (string && string[0]) copy_string(destination, capacity, string);
}

static void cache_twitch_headers(id request) {
    pthread_mutex_lock(&g_lock);
    cache_header(request, "Authorization", g_authorization, sizeof(g_authorization));
    cache_header(request, "Client-Integrity", g_integrity, sizeof(g_integrity));
    cache_header(request, "X-Device-Id", g_device_id, sizeof(g_device_id));
    if (!g_device_id[0]) cache_header(request, "Device-ID", g_device_id, sizeof(g_device_id));
    pthread_mutex_unlock(&g_lock);
}

static bool json_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static char *replace_json_string(char *source, const char *key, const char *replacement) {
    if (!source || !key || !replacement) return source;
    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t source_length = strlen(source);
    size_t replacement_length = strlen(replacement);
    size_t capacity = source_length + 64;
    size_t length = 0;
    char *result = calloc(capacity, 1);
    if (!result) return source;

    const char *cursor = source;
    const char *match;
    bool changed = false;
    while ((match = strstr(cursor, needle))) {
        const char *colon = match + strlen(needle);
        while (json_space(*colon)) colon++;
        if (*colon != ':') {
            size_t chunk = (size_t)(colon - cursor);
            if (length + chunk + 1 > capacity) {
                capacity = (length + chunk + 1) * 2;
                result = realloc(result, capacity);
            }
            memcpy(result + length, cursor, chunk);
            length += chunk;
            cursor = colon;
            continue;
        }
        const char *quote = colon + 1;
        while (json_space(*quote)) quote++;
        if (*quote != '"') {
            size_t chunk = (size_t)(quote - cursor);
            if (length + chunk + 1 > capacity) {
                capacity = (length + chunk + 1) * 2;
                result = realloc(result, capacity);
            }
            memcpy(result + length, cursor, chunk);
            length += chunk;
            cursor = quote;
            continue;
        }
        const char *value = quote + 1;
        const char *end = value;
        while (*end && (*end != '"' || (end > value && end[-1] == '\\'))) end++;
        if (!*end) break;
        size_t prefix = (size_t)(value - cursor);
        size_t needed = length + prefix + replacement_length + 1;
        if (needed > capacity) {
            capacity = needed * 2;
            result = realloc(result, capacity);
        }
        memcpy(result + length, cursor, prefix);
        length += prefix;
        memcpy(result + length, replacement, replacement_length);
        length += replacement_length;
        cursor = end;
        changed = true;
    }
    size_t tail = strlen(cursor);
    if (length + tail + 1 > capacity) {
        capacity = length + tail + 1;
        result = realloc(result, capacity);
    }
    memcpy(result + length, cursor, tail + 1);
    if (!changed) {
        free(result);
        return source;
    }
    free(source);
    return result;
}

static id normalized_graphql_body(id body) {
    char *json = copy_data_text(body);
    if (!json) return body;
    if (!(contains(json, "PlaybackAccessToken") || contains(json, "StreamAccessToken") ||
          contains(json, "streamPlaybackAccessToken"))) {
        free(json);
        return body;
    }
    json = replace_json_string(json, "playerType", "popout");
    id result = data_from_bytes(json, strlen(json));
    free(json);
    return result ?: body;
}

static id make_internal_request(const char *url, const char *method) {
    id request = msg1((id)objc_getClass("NSMutableURLRequest"), "requestWithURL:", nsurl(url));
    vmsg1(request, "setHTTPMethod:", nsstr(method));
    vmsg2(request, "setValue:forHTTPHeaderField:", nsstr("1"), nsstr(TAS_INTERNAL_HEADER));
    return request;
}

static id synchronous_request(id request, id *response, id *error) {
    return ((id (*)(id, SEL, id, id *, id *))objc_msgSend)(
        (id)objc_getClass("NSURLConnection"),
        sel_registerName("sendSynchronousRequest:returningResponse:error:"),
        request, response, error);
}

static void protocol_start_loading(id self, SEL command) {
    (void)command;
    id original = msg0(self, "request");
    id request = msg0(original, "mutableCopy");
    vmsg2(request, "setValue:forHTTPHeaderField:", nsstr("1"), nsstr(TAS_INTERNAL_HEADER));
    cache_twitch_headers(request);
    id body = msg0(request, "HTTPBody");
    if (body) vmsg1(request, "setHTTPBody:", normalized_graphql_body(body));

    id response = nil;
    id error = nil;
    id data = synchronous_request(request, &response, &error);
    id client = msg0(self, "client");
    if (error || !response) {
        vmsg2(client, "URLProtocol:didFailWithError:", self, error);
    } else {
        vmsg3(client, "URLProtocol:didReceiveResponse:cacheStoragePolicy:", self, response, 0);
        if (data) vmsg2(client, "URLProtocol:didLoadData:", self, data);
        vmsg1(client, "URLProtocolDidFinishLoading:", self);
    }
    objc_release(request);
}

static void protocol_stop_loading(id self, SEL command) {
    (void)self;
    (void)command;
}

static BOOL protocol_can_init(id self, SEL command, id request) {
    (void)self;
    (void)command;
    if (msg1(request, "valueForHTTPHeaderField:", nsstr(TAS_INTERNAL_HEADER))) return NO;
    id url = msg0(request, "URL");
    const char *host = utf8(msg0(url, "host"));
    return host && strcmp(host, "gql.twitch.tv") == 0;
}

static id protocol_canonical_request(id self, SEL command, id request) {
    (void)self;
    (void)command;
    return request;
}

static void append_text(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t incoming = strlen(text);
    if (*length + incoming + 1 > *capacity) {
        size_t next = *capacity ? *capacity : 4096;
        while (next < *length + incoming + 1) next *= 2;
        char *grown = realloc(*buffer, next);
        if (!grown) return;
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, text, incoming);
    *length += incoming;
    (*buffer)[*length] = '\0';
}

static char *absolute_url(const char *base, const char *relative) {
    if (starts_with(relative, "http://") || starts_with(relative, "https://")) return strdup(relative);
    id base_url = nsurl(base);
    id value = msg2((id)objc_getClass("NSURL"), "URLWithString:relativeToURL:", nsstr(relative), base_url);
    return strdup(utf8(msg0(value, "absoluteString")) ?: relative);
}

static char *custom_scheme_url(const char *value) {
    if (starts_with(value, "https://")) {
        size_t length = strlen(value) + strlen(TAS_SCHEME) + 1;
        char *result = malloc(length);
        snprintf(result, length, TAS_SCHEME "://%s", value + strlen("https://"));
        return result;
    }
    return strdup(value);
}

static char *https_scheme_url(const char *value) {
    if (starts_with(value, TAS_SCHEME "://")) {
        size_t length = strlen(value) + 1;
        char *result = malloc(length);
        snprintf(result, length, "https://%s", value + strlen(TAS_SCHEME "://"));
        return result;
    }
    return strdup(value);
}

static void parse_attribute(const char *line, const char *name, char *output, size_t capacity) {
    output[0] = '\0';
    const char *start = strstr(line, name);
    if (!start) return;
    start += strlen(name);
    if (*start == '"') start++;
    const char *end = start;
    while (*end && *end != ',' && *end != '"' && *end != '\r' && *end != '\n') end++;
    size_t length = (size_t)(end - start);
    if (length >= capacity) length = capacity - 1;
    memcpy(output, start, length);
    output[length] = '\0';
}

static void channel_from_master_url(const char *url, char *output, size_t capacity) {
    output[0] = '\0';
    const char *marker = strstr(url, "/channel/hls/");
    if (!marker) return;
    marker += strlen("/channel/hls/");
    const char *end = strstr(marker, ".m3u8");
    if (!end) return;
    size_t length = (size_t)(end - marker);
    if (length >= capacity) length = capacity - 1;
    memcpy(output, marker, length);
    output[length] = '\0';
}

static bool variant_metadata(const char *master, const char *variant_url,
                             char *resolution, size_t resolution_capacity,
                             char *framerate, size_t framerate_capacity) {
    char *copy = strdup(master);
    char *save = NULL;
    char *previous = NULL;
    bool found = false;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '#' && contains(line, ".m3u8")) {
            char *absolute = absolute_url(g_master_url, line);
            if (strcmp(absolute, variant_url) == 0 && previous && starts_with(previous, "#EXT-X-STREAM-INF")) {
                parse_attribute(previous, "RESOLUTION=", resolution, resolution_capacity);
                parse_attribute(previous, "FRAME-RATE=", framerate, framerate_capacity);
                found = true;
                free(absolute);
                break;
            }
            free(absolute);
        }
        previous = line;
    }
    free(copy);
    return found;
}

static char *variant_for_resolution(const char *master, const char *master_url,
                                    const char *resolution, const char *framerate) {
    char *copy = strdup(master);
    char *save = NULL;
    char *previous = NULL;
    char *fallback = NULL;
    char *resolution_match = NULL;
    char *exact = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '#' && contains(line, ".m3u8") && previous && starts_with(previous, "#EXT-X-STREAM-INF")) {
            char line_resolution[128];
            char line_framerate[64];
            parse_attribute(previous, "RESOLUTION=", line_resolution, sizeof(line_resolution));
            parse_attribute(previous, "FRAME-RATE=", line_framerate, sizeof(line_framerate));
            char *absolute = absolute_url(master_url, line);
            if (!fallback) fallback = strdup(absolute);
            if (resolution[0] && strcmp(line_resolution, resolution) == 0) {
                if (!resolution_match) resolution_match = strdup(absolute);
                if (!framerate[0] || strcmp(line_framerate, framerate) == 0) {
                    exact = strdup(absolute);
                    free(absolute);
                    break;
                }
            }
            free(absolute);
        }
        previous = line;
    }
    char *result = exact ?: resolution_match ?: fallback;
    if (result != resolution_match) free(resolution_match);
    if (result != fallback) free(fallback);
    free(copy);
    return result;
}

static char *url_encode(const char *value) {
    static const char hex[] = "0123456789ABCDEF";
    size_t length = strlen(value);
    char *result = malloc(length * 3 + 1);
    char *out = result;
    for (const unsigned char *in = (const unsigned char *)value; *in; in++) {
        if ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') ||
            (*in >= '0' && *in <= '9') || strchr("-._~", *in)) {
            *out++ = (char)*in;
        } else {
            *out++ = '%';
            *out++ = hex[*in >> 4];
            *out++ = hex[*in & 15];
        }
    }
    *out = '\0';
    return result;
}

static char *usher_with_token(const char *original, const char *signature, const char *token) {
    char *sig = url_encode(signature);
    char *tok = url_encode(token);
    const char *question = strchr(original, '?');
    size_t base_length = question ? (size_t)(question - original) : strlen(original);
    size_t capacity = strlen(original) + strlen(sig) + strlen(tok) + 64;
    char *result = calloc(capacity, 1);
    memcpy(result, original, base_length);
    result[base_length] = '\0';
    strcat(result, "?");
    if (question) {
        char *query = strdup(question + 1);
        char *save = NULL;
        for (char *item = strtok_r(query, "&", &save); item; item = strtok_r(NULL, "&", &save)) {
            if (starts_with(item, "sig=") || starts_with(item, "token=") || starts_with(item, "parent_domains=")) continue;
            strcat(result, item);
            strcat(result, "&");
        }
        free(query);
    }
    strcat(result, "sig=");
    strcat(result, sig);
    strcat(result, "&token=");
    strcat(result, tok);
    free(sig);
    free(tok);
    return result;
}

static id json_value(id object, const char *key) {
    return msg1(object, "objectForKeyedSubscript:", nsstr(key));
}

static bool fetch_bytes(const char *url, id *data, id *response) {
    id request = make_internal_request(url, "GET");
    id error = nil;
    *data = synchronous_request(request, response, &error);
    return *data && *response && !error && imsg0(*response, "statusCode") >= 200 && imsg0(*response, "statusCode") < 300;
}

static bool request_access_token(const char *channel, const char *player_type,
                                 char *signature, size_t signature_capacity,
                                 char *token, size_t token_capacity) {
    const char *platform = strcmp(player_type, "autoplay") == 0 ? "android" : "web";
    char json[8192];
    snprintf(json, sizeof(json),
        "{\"operationName\":\"PlaybackAccessToken\",\"variables\":{\"isLive\":true,"
        "\"login\":\"%s\",\"isVod\":false,\"vodID\":\"\",\"playerType\":\"%s\","
        "\"platform\":\"%s\"},\"extensions\":{\"persistedQuery\":{\"version\":1,"
        "\"sha256Hash\":\"%s\"}}}", channel, player_type, platform, TAS_TOKEN_HASH);

    id request = make_internal_request("https://gql.twitch.tv/gql", "POST");
    vmsg2(request, "setValue:forHTTPHeaderField:", nsstr("application/json"), nsstr("Content-Type"));
    pthread_mutex_lock(&g_lock);
    if (!g_device_id[0]) {
        static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (size_t i = 0; i < 32; i++) g_device_id[i] = alphabet[arc4random_uniform(36)];
        g_device_id[32] = '\0';
    }
    vmsg2(request, "setValue:forHTTPHeaderField:", nsstr(TAS_CLIENT_ID), nsstr("Client-Id"));
    if (g_device_id[0]) vmsg2(request, "setValue:forHTTPHeaderField:", nsstr(g_device_id), nsstr("X-Device-Id"));
    if (g_authorization[0]) vmsg2(request, "setValue:forHTTPHeaderField:", nsstr(g_authorization), nsstr("Authorization"));
    if (g_integrity[0]) vmsg2(request, "setValue:forHTTPHeaderField:", nsstr(g_integrity), nsstr("Client-Integrity"));
    pthread_mutex_unlock(&g_lock);
    vmsg1(request, "setHTTPBody:", data_from_bytes(json, strlen(json)));

    id response = nil;
    id error = nil;
    id data = synchronous_request(request, &response, &error);
    if (!data || error || imsg0(response, "statusCode") != 200) return false;
    id json_error = nil;
    id root = ((id (*)(id, SEL, id, NSUInteger, id *))objc_msgSend)(
        (id)objc_getClass("NSJSONSerialization"), sel_registerName("JSONObjectWithData:options:error:"),
        data, 0, &json_error);
    if (!root || json_error) return false;
    id access = json_value(json_value(root, "data"), "streamPlaybackAccessToken");
    const char *sig = utf8(json_value(access, "signature"));
    const char *tok = utf8(json_value(access, "value"));
    if (!sig || !tok) return false;
    copy_string(signature, signature_capacity, sig);
    copy_string(token, token_capacity, tok);
    return true;
}

static bool has_ad_markers(const char *playlist) {
    return contains(playlist, "stitched-ad") || contains(playlist, "twitch-stitched-ad") ||
           contains(playlist, "\"MIDROLL\"") || contains(playlist, "\"midroll\"");
}

static char *fetch_clean_variant(const char *original_master, const char *channel,
                                 const char *resolution, const char *framerate) {
    const char *player_types[] = {"autoplay", "picture-by-picture", "embed"};
    for (size_t i = 0; i < sizeof(player_types) / sizeof(player_types[0]); i++) {
        char signature[4096];
        char token[16384];
        if (!request_access_token(channel, player_types[i], signature, sizeof(signature), token, sizeof(token))) continue;
        char *master_url = usher_with_token(original_master, signature, token);
        id master_data = nil;
        id master_response = nil;
        if (!fetch_bytes(master_url, &master_data, &master_response)) {
            free(master_url);
            continue;
        }
        char *master = copy_data_text(master_data);
        char *variant_url = variant_for_resolution(master, master_url, resolution, framerate);
        id variant_data = nil;
        id variant_response = nil;
        bool loaded = variant_url && fetch_bytes(variant_url, &variant_data, &variant_response);
        char *variant = loaded ? copy_data_text(variant_data) : NULL;
        free(master);
        free(master_url);
        if (variant && !has_ad_markers(variant)) {
            pthread_mutex_lock(&g_lock);
            copy_string(g_backup_variant_url, sizeof(g_backup_variant_url), variant_url);
            g_backup_created = time(NULL);
            pthread_mutex_unlock(&g_lock);
            fprintf(stderr, "[TAS] Switched %s to clean %s HLS\n", channel, player_types[i]);
            free(variant_url);
            return variant;
        }
        free(variant_url);
        free(variant);
    }
    return NULL;
}

static char *strip_ad_segments(const char *playlist) {
    char *copy = strdup(playlist);
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char *save = NULL;
    bool drop_next_uri = false;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (drop_next_uri && line[0] != '#') {
            drop_next_uri = false;
            continue;
        }
        if (starts_with(line, "#EXTINF") && !contains(line, ",live")) {
            drop_next_uri = true;
            continue;
        }
        if (starts_with(line, "#EXT-X-TWITCH-PREFETCH:") || contains(line, "stitched-ad")) continue;
        append_text(&result, &length, &capacity, line);
        append_text(&result, &length, &capacity, "\n");
    }
    free(copy);
    return result ?: strdup("#EXTM3U\n");
}

static char *rewrite_manifest_urls(const char *playlist, const char *base_url) {
    char *copy = strdup(playlist);
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '#' && line[0]) {
            char *absolute = absolute_url(base_url, line);
            char *custom = custom_scheme_url(absolute);
            append_text(&result, &length, &capacity, custom);
            free(custom);
            free(absolute);
        } else if (starts_with(line, "#EXT-X-TWITCH-PREFETCH:")) {
            const char *value = line + strlen("#EXT-X-TWITCH-PREFETCH:");
            char *absolute = absolute_url(base_url, value);
            char *custom = custom_scheme_url(absolute);
            append_text(&result, &length, &capacity, "#EXT-X-TWITCH-PREFETCH:");
            append_text(&result, &length, &capacity, custom);
            free(custom);
            free(absolute);
        } else {
            char *uri = strstr(line, "URI=\"");
            if (uri) {
                const char *value = uri + strlen("URI=\"");
                const char *end = strchr(value, '"');
                if (end) {
                    char relative[8192];
                    size_t size = (size_t)(end - value);
                    if (size >= sizeof(relative)) size = sizeof(relative) - 1;
                    memcpy(relative, value, size);
                    relative[size] = '\0';
                    char *absolute = absolute_url(base_url, relative);
                    char *custom = custom_scheme_url(absolute);
                    char prefix[16384];
                    size_t prefix_length = (size_t)(value - line);
                    if (prefix_length >= sizeof(prefix)) prefix_length = sizeof(prefix) - 1;
                    memcpy(prefix, line, prefix_length);
                    prefix[prefix_length] = '\0';
                    append_text(&result, &length, &capacity, prefix);
                    append_text(&result, &length, &capacity, custom);
                    append_text(&result, &length, &capacity, end);
                    free(custom);
                    free(absolute);
                } else {
                    append_text(&result, &length, &capacity, line);
                }
            } else {
                append_text(&result, &length, &capacity, line);
            }
        }
        append_text(&result, &length, &capacity, "\n");
    }
    free(copy);
    return result ?: strdup(playlist);
}

static char *process_manifest(const char *url, const char *playlist) {
    char channel[512];
    channel_from_master_url(url, channel, sizeof(channel));
    if (channel[0]) {
        pthread_mutex_lock(&g_lock);
        copy_string(g_channel, sizeof(g_channel), channel);
        copy_string(g_master_url, sizeof(g_master_url), url);
        g_backup_variant_url[0] = '\0';
        pthread_mutex_unlock(&g_lock);
        return rewrite_manifest_urls(playlist, url);
    }

    char master_url[8192];
    char current_channel[512];
    char resolution[128] = "";
    char framerate[64] = "";
    char backup_url[8192];
    pthread_mutex_lock(&g_lock);
    copy_string(master_url, sizeof(master_url), g_master_url);
    copy_string(current_channel, sizeof(current_channel), g_channel);
    copy_string(backup_url, sizeof(backup_url), g_backup_variant_url);
    pthread_mutex_unlock(&g_lock);

    if (master_url[0]) {
        id master_data = nil;
        id master_response = nil;
        if (fetch_bytes(master_url, &master_data, &master_response)) {
            char *master = copy_data_text(master_data);
            variant_metadata(master, url, resolution, sizeof(resolution), framerate, sizeof(framerate));
            free(master);
        }
    }

    char *selected = NULL;
    char *selected_base = NULL;
    if (has_ad_markers(playlist)) {
        if (backup_url[0] && time(NULL) - g_backup_created < 600) {
            id backup_data = nil;
            id backup_response = nil;
            if (fetch_bytes(backup_url, &backup_data, &backup_response)) {
                char *candidate = copy_data_text(backup_data);
                if (candidate && !has_ad_markers(candidate)) {
                    selected = candidate;
                    selected_base = strdup(backup_url);
                } else {
                    free(candidate);
                }
            }
        }
        if (!selected && master_url[0] && current_channel[0]) {
            selected = fetch_clean_variant(master_url, current_channel, resolution, framerate);
            if (selected) {
                pthread_mutex_lock(&g_lock);
                selected_base = strdup(g_backup_variant_url[0] ? g_backup_variant_url : url);
                pthread_mutex_unlock(&g_lock);
            }
        }
        if (!selected) {
            selected = strip_ad_segments(playlist);
            selected_base = strdup(url);
            fprintf(stderr, "[TAS] All alternate players had ads; stripping stitched segments\n");
        }
    } else {
        pthread_mutex_lock(&g_lock);
        g_backup_variant_url[0] = '\0';
        pthread_mutex_unlock(&g_lock);
        selected = strdup(playlist);
        selected_base = strdup(url);
    }
    char *rewritten = rewrite_manifest_urls(selected, selected_base);
    free(selected);
    free(selected_base);
    return rewritten;
}

static BOOL loader_handle(id self, SEL command, id loading_request) {
    (void)self;
    (void)command;
    id request = msg0(loading_request, "request");
    id original_url = msg0(request, "URL");
    const char *custom_url = utf8(msg0(original_url, "absoluteString"));
    if (!custom_url) return NO;
    char *url = https_scheme_url(custom_url);
    id network_request = msg0(request, "mutableCopy");
    vmsg1(network_request, "setURL:", nsurl(url));
    vmsg2(network_request, "setValue:forHTTPHeaderField:", nsstr("1"), nsstr(TAS_INTERNAL_HEADER));

    id response = nil;
    id error = nil;
    id data = synchronous_request(network_request, &response, &error);
    if (error || !data || !response) {
        vmsg1(loading_request, "finishLoadingWithError:", error);
        objc_release(network_request);
        free(url);
        return YES;
    }

    const char *mime = utf8(msg0(response, "MIMEType"));
    char *text = NULL;
    id output_data = data;
    if (contains(url, ".m3u8") || (mime && contains(mime, "mpegurl"))) {
        text = copy_data_text(data);
        if (text && starts_with(text, "#EXTM3U")) {
            char *processed = process_manifest(url, text);
            output_data = data_from_bytes(processed, strlen(processed));
            free(processed);
        }
        free(text);
    }

    id content = msg0(loading_request, "contentInformationRequest");
    if (content) {
        const char *type = contains(url, ".m3u8") ? "public.m3u-playlist" :
                           (contains(url, ".ts") ? "public.mpeg-2-transport-stream" : "public.mpeg-4");
        vmsg1(content, "setContentType:", nsstr(type));
        ((void (*)(id, SEL, long long))objc_msgSend)(content, sel_registerName("setContentLength:"), (long long)data_length(output_data));
        ((void (*)(id, SEL, BOOL))objc_msgSend)(content, sel_registerName("setByteRangeAccessSupported:"), YES);
    }
    id data_request = msg0(loading_request, "dataRequest");
    if (data_request) vmsg1(data_request, "respondWithData:", output_data);
    msg0(loading_request, "finishLoading");
    objc_release(network_request);
    free(url);
    return YES;
}

static BOOL loader_wait(id self, SEL command, id resource_loader, id loading_request) {
    (void)resource_loader;
    return loader_handle(self, command, loading_request);
}

static id asset_init(id self, SEL command, id url, id options) {
    const char *absolute = utf8(msg0(url, "absoluteString"));
    if (!absolute || !starts_with(absolute, "https://") || !contains(absolute, ".m3u8") ||
        !(contains(absolute, "ttvnw.net") || contains(absolute, "twitch.tv"))) {
        return ((id (*)(id, SEL, id, id))g_original_asset_init)(self, command, url, options);
    }
    char *custom = custom_scheme_url(absolute);
    id asset = ((id (*)(id, SEL, id, id))g_original_asset_init)(self, command, nsurl(custom), options);
    free(custom);
    if (!asset) return asset;
    id delegate = msg0((id)g_loader_class, "new");
    id loader = msg0(asset, "resourceLoader");
    dispatch_queue_t queue = dispatch_queue_create("dev.tas.hls-loader", DISPATCH_QUEUE_SERIAL);
    ((void (*)(id, SEL, id, dispatch_queue_t))objc_msgSend)(
        loader, sel_registerName("setDelegate:queue:"), delegate, queue);
    objc_setAssociatedObject(asset, &g_association_key, delegate, 1);
    objc_release(delegate);
    return asset;
}

static void add_protocol_to_configuration(id configuration) {
    if (!configuration || !g_protocol_class) return;
    id classes = msg0(configuration, "protocolClasses");
    id mutable = classes ? msg0(classes, "mutableCopy") : msg0((id)objc_getClass("NSMutableArray"), "array");
    if (!bmsg1(mutable, "containsObject:", (id)g_protocol_class)) {
        ((void (*)(id, SEL, id, NSUInteger))objc_msgSend)(
            mutable, sel_registerName("insertObject:atIndex:"), (id)g_protocol_class, 0);
    }
    vmsg1(configuration, "setProtocolClasses:", mutable);
    if (classes) objc_release(mutable);
}

static id default_configuration(id self, SEL command) {
    id result = ((id (*)(id, SEL))g_original_default_configuration)(self, command);
    add_protocol_to_configuration(result);
    return result;
}

static id ephemeral_configuration(id self, SEL command) {
    id result = ((id (*)(id, SEL))g_original_ephemeral_configuration)(self, command);
    add_protocol_to_configuration(result);
    return result;
}

static void swizzle_method(Class cls, const char *selector, IMP replacement, IMP *original, bool class_method) {
    Method method = class_method ? class_getClassMethod(cls, sel_registerName(selector)) :
                                   class_getInstanceMethod(cls, sel_registerName(selector));
    if (!method) return;
    if (original) *original = method_getImplementation(method);
    method_setImplementation(method, replacement);
}

__attribute__((constructor))
static void tas_initialize(void) {
    Class url_protocol = objc_getClass("NSURLProtocol");
    g_protocol_class = objc_allocateClassPair(url_protocol, "TASURLProtocol", 0);
    if (g_protocol_class) {
        class_addMethod(object_getClass((id)g_protocol_class), sel_registerName("canInitWithRequest:"), (IMP)protocol_can_init, "B@:@");
        class_addMethod(object_getClass((id)g_protocol_class), sel_registerName("canonicalRequestForRequest:"), (IMP)protocol_canonical_request, "@@:@");
        class_addMethod(g_protocol_class, sel_registerName("startLoading"), (IMP)protocol_start_loading, "v@:");
        class_addMethod(g_protocol_class, sel_registerName("stopLoading"), (IMP)protocol_stop_loading, "v@:");
        objc_registerClassPair(g_protocol_class);
        ((BOOL (*)(id, SEL, Class))objc_msgSend)((id)url_protocol, sel_registerName("registerClass:"), g_protocol_class);
    }

    g_loader_class = objc_allocateClassPair(objc_getClass("NSObject"), "TASAssetResourceLoaderDelegate", 0);
    if (g_loader_class) {
        class_addMethod(g_loader_class, sel_registerName("resourceLoader:shouldWaitForLoadingOfRequestedResource:"),
                        (IMP)loader_wait, "B@:@@");
        class_addMethod(g_loader_class, sel_registerName("resourceLoader:shouldWaitForRenewalOfRequestedResource:"),
                        (IMP)loader_wait, "B@:@@");
        objc_registerClassPair(g_loader_class);
    }

    Class asset = objc_getClass("AVURLAsset");
    swizzle_method(asset, "initWithURL:options:", (IMP)asset_init, &g_original_asset_init, false);

    Class configuration = objc_getClass("NSURLSessionConfiguration");
    swizzle_method(configuration, "defaultSessionConfiguration", (IMP)default_configuration,
                   &g_original_default_configuration, true);
    swizzle_method(configuration, "ephemeralSessionConfiguration", (IMP)ephemeral_configuration,
                   &g_original_ephemeral_configuration, true);
    fprintf(stderr, "[TAS] TwitchAdSolutions iOS port loaded\n");
}
