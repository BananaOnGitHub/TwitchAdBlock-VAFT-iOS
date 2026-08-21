#include "TASDiagnostics.h"

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef double CGFloat;

typedef struct {
    CGFloat x;
    CGFloat y;
} TASPoint;

typedef struct {
    CGFloat width;
    CGFloat height;
} TASSize;

typedef struct {
    TASPoint origin;
    TASSize size;
} TASRect;

#define TAS_DIAGNOSTICS_KEY "TASDiagnosticsEnabled"
#define TAS_DIAGNOSTICS_DIRECTORY "TwitchAdBlock-VAFT"
#define TAS_DIAGNOSTICS_FILENAME "diagnostics.log"
#define TAS_DIAGNOSTICS_LIMIT (512ULL * 1024ULL)
#define TAS_REPORT_VERSION "2.2.0"
#define TAS_LOADED_NOTICE_KEY "TASLoadedNoticeShown220R8"

extern id objc_retain(id object);
extern void objc_release(id object);
extern void objc_setAssociatedObject(id object, const void *key, id value, uintptr_t policy);
extern id objc_getAssociatedObject(id object, const void *key);

static pthread_mutex_t g_diag_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_metrics[TAS_DIAG_METRIC_COUNT];
static uint64_t g_logged_events;
static id g_diagnostics_path;
static Class g_settings_class;
static Class g_log_class;
static IMP g_settings_super_view_did_load;
static IMP g_settings_super_view_will_appear;
static IMP g_log_super_view_did_load;
static IMP g_log_super_view_will_appear;
static IMP g_app_settings_original_view_did_appear;
static IMP g_view_controller_original_view_did_appear;
static Class g_bootstrap_class;
static id g_bootstrap_observer;
static bool g_app_settings_hooked;
static char g_log_text_view_key;

static id msg0(id object, const char *selector) {
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static id msg1(id object, const char *selector, id a) {
    return ((id (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), a);
}

static void vmsg1(id object, const char *selector, id a) {
    ((void (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), a);
}

static void vmsg_integer(id object, const char *selector, NSInteger value) {
    ((void (*)(id, SEL, NSInteger))objc_msgSend)(object, sel_registerName(selector), value);
}

static void vmsg_bool(id object, const char *selector, BOOL value) {
    ((void (*)(id, SEL, BOOL))objc_msgSend)(object, sel_registerName(selector), value);
}

static BOOL bmsg0(id object, const char *selector) {
    return ((BOOL (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static BOOL bmsg1(id object, const char *selector, id value) {
    return ((BOOL (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(selector), value);
}

static NSInteger imsg0(id object, const char *selector) {
    return ((NSInteger (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static id nsstr(const char *value) {
    if (!value) value = "";
    return msg1((id)objc_getClass("NSString"), "stringWithUTF8String:", (id)value);
}

static const char *utf8(id value) {
    return value ? ((const char *(*)(id, SEL))objc_msgSend)(value, sel_registerName("UTF8String")) : NULL;
}

static id data_from_bytes(const void *bytes, size_t length) {
    return ((id (*)(id, SEL, const void *, NSUInteger))objc_msgSend)(
        (id)objc_getClass("NSData"), sel_registerName("dataWithBytes:length:"), bytes, (NSUInteger)length);
}

static size_t data_length(id data) {
    return data ? (size_t)((NSUInteger (*)(id, SEL))objc_msgSend)(data, sel_registerName("length")) : 0;
}

static id defaults(void) {
    return msg0((id)objc_getClass("NSUserDefaults"), "standardUserDefaults");
}

static bool diagnostics_enabled(void) {
    return bmsg1(defaults(), "boolForKey:", nsstr(TAS_DIAGNOSTICS_KEY));
}

static void set_diagnostics_enabled(bool enabled) {
    ((void (*)(id, SEL, BOOL, id))objc_msgSend)(
        defaults(), sel_registerName("setBool:forKey:"), enabled ? YES : NO, nsstr(TAS_DIAGNOSTICS_KEY));
}

static id diagnostics_path_locked(void) {
    if (g_diagnostics_path) return g_diagnostics_path;

    id manager = msg0((id)objc_getClass("NSFileManager"), "defaultManager");
    id urls = ((id (*)(id, SEL, NSUInteger, NSUInteger))objc_msgSend)(
        manager, sel_registerName("URLsForDirectory:inDomains:"), (NSUInteger)14, (NSUInteger)1);
    id base_url = msg0(urls, "firstObject");
    id base_path = msg0(base_url, "path");
    if (!base_path) {
        id environment = msg0(msg0((id)objc_getClass("NSProcessInfo"), "processInfo"), "environment");
        id home = msg1(environment, "objectForKey:", nsstr("HOME"));
        base_path = msg1(home, "stringByAppendingPathComponent:", nsstr("Library/Application Support"));
    }
    if (!base_path) return nil;

    id directory = msg1(base_path, "stringByAppendingPathComponent:", nsstr(TAS_DIAGNOSTICS_DIRECTORY));
    ((BOOL (*)(id, SEL, id, BOOL, id, id *))objc_msgSend)(
        manager, sel_registerName("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"),
        directory, YES, nil, NULL);
    id path = msg1(directory, "stringByAppendingPathComponent:", nsstr(TAS_DIAGNOSTICS_FILENAME));
    g_diagnostics_path = objc_retain(path);
    return g_diagnostics_path;
}

void tas_diag_metric(TASDiagnosticMetric metric, uint64_t amount) {
    if (metric < 0 || metric >= TAS_DIAG_METRIC_COUNT) return;
    pthread_mutex_lock(&g_diag_lock);
    g_metrics[metric] += amount;
    pthread_mutex_unlock(&g_diag_lock);
}

static void append_log_line_locked(const char *line) {
    id path = diagnostics_path_locked();
    if (!path) return;
    id manager = msg0((id)objc_getClass("NSFileManager"), "defaultManager");
    id handle = msg1((id)objc_getClass("NSFileHandle"), "fileHandleForWritingAtPath:", path);
    if (!handle) {
        ((BOOL (*)(id, SEL, id, id, id))objc_msgSend)(
            manager, sel_registerName("createFileAtPath:contents:attributes:"),
            path, data_from_bytes("", 0), nil);
        handle = msg1((id)objc_getClass("NSFileHandle"), "fileHandleForWritingAtPath:", path);
    }
    if (!handle) return;

    unsigned long long offset = ((unsigned long long (*)(id, SEL))objc_msgSend)(
        handle, sel_registerName("seekToEndOfFile"));
    if (offset >= TAS_DIAGNOSTICS_LIMIT) {
        ((void (*)(id, SEL, unsigned long long))objc_msgSend)(
            handle, sel_registerName("truncateFileAtOffset:"), 0ULL);
        ((void (*)(id, SEL, unsigned long long))objc_msgSend)(
            handle, sel_registerName("seekToFileOffset:"), 0ULL);
        char rotated[160];
        snprintf(rotated, sizeof(rotated), "[%lld] LOG_ROTATED previous log exceeded 512 KiB\n",
                 (long long)time(NULL));
        vmsg1(handle, "writeData:", data_from_bytes(rotated, strlen(rotated)));
    }
    vmsg1(handle, "writeData:", data_from_bytes(line, strlen(line)));
    msg0(handle, "synchronizeFile");
    msg0(handle, "closeFile");
}

void tas_diag_log(const char *event, const char *detail) {
    if (!diagnostics_enabled()) return;
    char line[2048];
    snprintf(line, sizeof(line), "[%lld] %s%s%s\n", (long long)time(NULL),
             event ? event : "EVENT", detail && detail[0] ? " " : "", detail ? detail : "");
    pthread_mutex_lock(&g_diag_lock);
    g_logged_events++;
    append_log_line_locked(line);
    pthread_mutex_unlock(&g_diag_lock);
}

static void sanitized_url(const char *url, char *output, size_t capacity) {
    if (!output || !capacity) return;
    output[0] = '\0';
    if (!url) return;
    const char *end = strchr(url, '?');
    const char *fragment = strchr(url, '#');
    if (!end || (fragment && fragment < end)) end = fragment;
    size_t length = end ? (size_t)(end - url) : strlen(url);
    if (length >= capacity) length = capacity - 1;
    memcpy(output, url, length);
    output[length] = '\0';
}

void tas_diag_log_url(const char *event, const char *url, const char *detail) {
    if (!diagnostics_enabled()) return;
    char safe_url[1024];
    char combined[1536];
    sanitized_url(url, safe_url, sizeof(safe_url));
    snprintf(combined, sizeof(combined), "url=%s%s%s", safe_url,
             detail && detail[0] ? " " : "", detail ? detail : "");
    tas_diag_log(event, combined);
}

static id diagnostic_log_data_locked(void) {
    id path = diagnostics_path_locked();
    if (!path) return nil;
    return msg1((id)objc_getClass("NSData"), "dataWithContentsOfFile:", path);
}

static id diagnostic_report_create(void) {
    uint64_t metrics[TAS_DIAG_METRIC_COUNT];
    uint64_t events;
    id log_data;
    pthread_mutex_lock(&g_diag_lock);
    memcpy(metrics, g_metrics, sizeof(metrics));
    events = g_logged_events;
    log_data = objc_retain(diagnostic_log_data_locked());
    pthread_mutex_unlock(&g_diag_lock);

    id bundle = msg0((id)objc_getClass("NSBundle"), "mainBundle");
    const char *app_version = utf8(msg1(bundle, "objectForInfoDictionaryKey:", nsstr("CFBundleShortVersionString")));
    const char *app_build = utf8(msg1(bundle, "objectForInfoDictionaryKey:", nsstr("CFBundleVersion")));
    char header[4096];
    snprintf(header, sizeof(header),
        "TwitchAdBlock VAFT iOS diagnostic report\n"
        "Port build: %s\n"
        "Twitch: %s (%s)\n"
        "Diagnostic logging: %s\n"
        "Privacy: URL query strings/fragments, headers, access tokens, and manifest contents are not stored.\n"
        "Log limit: 512 KiB\n\n"
        "Session counters\n"
        "HLS intercepted: %llu\n"
        "Master manifests: %llu\n"
        "Variant manifests: %llu\n"
        "Ad-marked manifests: %llu\n"
        "Clean alternate swaps: %llu\n"
        "Suppressed ad segments: %llu\n"
        "Access-token failures: %llu\n"
        "Unmapped variants: %llu\n"
        "Synthetic segment responses: %llu\n"
        "GraphQL rewrites: %llu\n"
        "HLS failures: %llu\n"
        "Logged events this launch: %llu\n\n--- Log ---\n",
        TAS_REPORT_VERSION, app_version ? app_version : "unknown", app_build ? app_build : "unknown",
        diagnostics_enabled() ? "enabled" : "disabled",
        (unsigned long long)metrics[TAS_DIAG_HLS_INTERCEPTED],
        (unsigned long long)metrics[TAS_DIAG_MASTER_MANIFEST],
        (unsigned long long)metrics[TAS_DIAG_VARIANT_MANIFEST],
        (unsigned long long)metrics[TAS_DIAG_AD_MANIFEST],
        (unsigned long long)metrics[TAS_DIAG_CLEAN_ALTERNATE],
        (unsigned long long)metrics[TAS_DIAG_STRIPPED_SEGMENT],
        (unsigned long long)metrics[TAS_DIAG_TOKEN_FAILURE],
        (unsigned long long)metrics[TAS_DIAG_UNMAPPED_VARIANT],
        (unsigned long long)metrics[TAS_DIAG_SYNTHETIC_SEGMENT],
        (unsigned long long)metrics[TAS_DIAG_GRAPHQL_REWRITE],
        (unsigned long long)metrics[TAS_DIAG_HLS_FAILURE],
        (unsigned long long)events);

    id report = msg1((id)objc_getClass("NSMutableString"), "stringWithString:", nsstr(header));
    if (log_data && data_length(log_data)) {
        id log_text = msg0((id)objc_getClass("NSString"), "alloc");
        log_text = ((id (*)(id, SEL, id, NSUInteger))objc_msgSend)(
            log_text, sel_registerName("initWithData:encoding:"), log_data, (NSUInteger)4);
        if (log_text) {
            vmsg1(report, "appendString:", log_text);
            objc_release(log_text);
        }
    } else {
        vmsg1(report, "appendString:", nsstr("No diagnostic entries yet.\n"));
    }
    if (log_data) objc_release(log_data);
    return objc_retain(report);
}

static void clear_diagnostic_log(void) {
    pthread_mutex_lock(&g_diag_lock);
    id path = diagnostics_path_locked();
    if (path) {
        ((BOOL (*)(id, SEL, id, BOOL))objc_msgSend)(
            data_from_bytes("", 0), sel_registerName("writeToFile:atomically:"), path, YES);
    }
    g_logged_events = 0;
    pthread_mutex_unlock(&g_diag_lock);
    tas_diag_log("LOG_CLEARED", "The on-disk diagnostic log was cleared");
}

static id make_bar_button(const char *title, id target, const char *action) {
    id item = msg0((id)objc_getClass("UIBarButtonItem"), "alloc");
    return ((id (*)(id, SEL, id, NSInteger, id, SEL))objc_msgSend)(
        item, sel_registerName("initWithTitle:style:target:action:"),
        nsstr(title), (NSInteger)0, target, sel_registerName(action));
}

static void show_notice(id controller, const char *title, const char *message) {
    id alert = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)(
        (id)objc_getClass("UIAlertController"),
        sel_registerName("alertControllerWithTitle:message:preferredStyle:"),
        nsstr(title), nsstr(message), (NSInteger)1);
    id action = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)(
        (id)objc_getClass("UIAlertAction"), sel_registerName("actionWithTitle:style:handler:"),
        nsstr("OK"), (NSInteger)0, nil);
    vmsg1(alert, "addAction:", action);
    ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(
        controller, sel_registerName("presentViewController:animated:completion:"), alert, YES, nil);
}

static id table_cell(NSInteger style) {
    id cell = msg0((id)objc_getClass("UITableViewCell"), "alloc");
    cell = ((id (*)(id, SEL, NSInteger, id))objc_msgSend)(
        cell, sel_registerName("initWithStyle:reuseIdentifier:"), style, nil);
    return cell;
}

static void set_cell_text(id cell, const char *title, const char *detail) {
    vmsg1(msg0(cell, "textLabel"), "setText:", nsstr(title));
    if (detail) vmsg1(msg0(cell, "detailTextLabel"), "setText:", nsstr(detail));
}

static void settings_view_did_load(id self, SEL command) {
    if (g_settings_super_view_did_load) {
        ((void (*)(id, SEL))g_settings_super_view_did_load)(self, command);
    }
    vmsg1(self, "setTitle:", nsstr("Ad Block"));
}

static void settings_view_will_appear(id self, SEL command, BOOL animated) {
    if (g_settings_super_view_will_appear) {
        ((void (*)(id, SEL, BOOL))g_settings_super_view_will_appear)(self, command, animated);
    }
    msg0(msg0(self, "tableView"), "reloadData");
}

static NSInteger settings_number_of_sections(id self, SEL command, id table) {
    (void)self;
    (void)command;
    (void)table;
    return 3;
}

static NSInteger settings_rows_in_section(id self, SEL command, id table, NSInteger section) {
    (void)self;
    (void)command;
    (void)table;
    if (section == 0 || section == 1) return 1;
    if (section == 2) return 3;
    return 0;
}

static id settings_header(id self, SEL command, id table, NSInteger section) {
    (void)self;
    (void)command;
    (void)table;
    if (section == 0) return nsstr("Status");
    if (section == 1) return nsstr("Diagnostics");
    if (section == 2) return nsstr("Diagnostic Report");
    return nil;
}

static id settings_footer(id self, SEL command, id table, NSInteger section) {
    (void)self;
    (void)command;
    (void)table;
    if (section == 1) {
        return nsstr("Records sanitized request paths, response status, manifest marker summaries, and VAFT decisions. Query strings, headers, access tokens, and manifest contents are never stored. The log is capped at 512 KiB.");
    }
    return nil;
}

static id settings_cell(id self, SEL command, id table, id index_path) {
    (void)self;
    (void)command;
    (void)table;
    NSInteger section = imsg0(index_path, "section");
    NSInteger row = imsg0(index_path, "row");
    id cell = table_cell(1);
    if (section == 0) {
        set_cell_text(cell, "Ad Blocking", "Enabled · VAFT v24");
        vmsg_integer(cell, "setSelectionStyle:", 0);
    } else if (section == 1) {
        set_cell_text(cell, "Diagnostic Logging", diagnostics_enabled() ? "Enabled" : "Disabled");
        id toggle = msg0(msg0((id)objc_getClass("UISwitch"), "alloc"), "init");
        ((void (*)(id, SEL, BOOL, BOOL))objc_msgSend)(
            toggle, sel_registerName("setOn:animated:"), diagnostics_enabled() ? YES : NO, NO);
        ((void (*)(id, SEL, id, SEL, NSUInteger))objc_msgSend)(
            toggle, sel_registerName("addTarget:action:forControlEvents:"),
            self, sel_registerName("tas_diagnosticsSwitchChanged:"), (NSUInteger)(1UL << 12));
        vmsg1(toggle, "setAccessibilityIdentifier:", nsstr("TASDiagnosticsSwitch"));
        vmsg1(cell, "setAccessoryView:", toggle);
        vmsg_integer(cell, "setSelectionStyle:", 0);
        objc_release(toggle);
    } else if (section == 2 && row == 0) {
        set_cell_text(cell, "View Diagnostic Report", "Session summary and sanitized event log");
        vmsg_integer(cell, "setAccessoryType:", 1);
    } else if (section == 2 && row == 1) {
        set_cell_text(cell, "Copy Diagnostic Report", "Copies the complete report to the clipboard");
    } else {
        set_cell_text(cell, "Clear Diagnostic Log", "Removes stored event entries");
    }
    return msg0(cell, "autorelease");
}

static void settings_switch_changed(id self, SEL command, id sender) {
    (void)command;
    bool enabled = bmsg0(sender, "isOn");
    set_diagnostics_enabled(enabled);
    if (enabled) tas_diag_log("LOGGING_ENABLED", "Diagnostic logging enabled from Twitch settings");
    msg0(msg0(self, "tableView"), "reloadData");
}

static void update_log_text(id self) {
    id text_view = objc_getAssociatedObject(self, &g_log_text_view_key);
    if (!text_view) return;
    id report = diagnostic_report_create();
    vmsg1(text_view, "setText:", report);
    objc_release(report);
}

static void log_copy(id self, SEL command) {
    (void)command;
    id report = diagnostic_report_create();
    vmsg1(msg0((id)objc_getClass("UIPasteboard"), "generalPasteboard"), "setString:", report);
    objc_release(report);
    show_notice(self, "Copied", "The diagnostic report was copied to the clipboard.");
}

static void log_view_did_load(id self, SEL command) {
    if (g_log_super_view_did_load) {
        ((void (*)(id, SEL))g_log_super_view_did_load)(self, command);
    }
    vmsg1(self, "setTitle:", nsstr("Diagnostic Report"));
    id parent = msg0(self, "view");
    TASRect bounds = ((TASRect (*)(id, SEL))objc_msgSend)(parent, sel_registerName("bounds"));
    id text_view = msg0((id)objc_getClass("UITextView"), "alloc");
    text_view = ((id (*)(id, SEL, TASRect))objc_msgSend)(
        text_view, sel_registerName("initWithFrame:"), bounds);
    vmsg_integer(text_view, "setAutoresizingMask:", (NSInteger)18);
    vmsg_bool(text_view, "setEditable:", NO);
    vmsg_bool(text_view, "setSelectable:", YES);
    vmsg_bool(text_view, "setAlwaysBounceVertical:", YES);
    id font = ((id (*)(id, SEL, CGFloat, CGFloat))objc_msgSend)(
        (id)objc_getClass("UIFont"), sel_registerName("monospacedSystemFontOfSize:weight:"),
        (CGFloat)12.0, (CGFloat)0.0);
    vmsg1(text_view, "setFont:", font);
    vmsg1(text_view, "setBackgroundColor:", msg0((id)objc_getClass("UIColor"), "systemBackgroundColor"));
    vmsg1(text_view, "setTextColor:", msg0((id)objc_getClass("UIColor"), "labelColor"));
    vmsg1(parent, "addSubview:", text_view);
    objc_setAssociatedObject(self, &g_log_text_view_key, text_view, 1);
    objc_release(text_view);

    id copy = make_bar_button("Copy", self, "tas_copyDiagnosticReport");
    vmsg1(msg0(self, "navigationItem"), "setRightBarButtonItem:", copy);
    objc_release(copy);
    update_log_text(self);
}

static void log_view_will_appear(id self, SEL command, BOOL animated) {
    if (g_log_super_view_will_appear) {
        ((void (*)(id, SEL, BOOL))g_log_super_view_will_appear)(self, command, animated);
    }
    update_log_text(self);
}

static void settings_did_select(id self, SEL command, id table, id index_path) {
    (void)command;
    NSInteger section = imsg0(index_path, "section");
    NSInteger row = imsg0(index_path, "row");
    ((void (*)(id, SEL, id, BOOL))objc_msgSend)(
        table, sel_registerName("deselectRowAtIndexPath:animated:"), index_path, YES);
    if (section != 2) return;
    if (row == 0) {
        id controller = msg0((id)g_log_class, "new");
        ((void (*)(id, SEL, id, BOOL))objc_msgSend)(
            msg0(self, "navigationController"), sel_registerName("pushViewController:animated:"),
            controller, YES);
        objc_release(controller);
    } else if (row == 1) {
        id report = diagnostic_report_create();
        vmsg1(msg0((id)objc_getClass("UIPasteboard"), "generalPasteboard"), "setString:", report);
        objc_release(report);
        show_notice(self, "Copied", "The diagnostic report was copied to the clipboard.");
    } else if (row == 2) {
        clear_diagnostic_log();
        msg0(table, "reloadData");
        show_notice(self, "Cleared", "The stored diagnostic event log was cleared.");
    }
}

static void open_ad_block_settings(id self, SEL command) {
    (void)command;
    if (g_settings_class && ((BOOL (*)(id, SEL, Class))objc_msgSend)(
            self, sel_registerName("isKindOfClass:"), g_settings_class)) return;

    id navigation = msg0(self, "navigationController");
    id controllers = msg0(navigation, "viewControllers");
    NSInteger count = imsg0(controllers, "count");
    for (NSInteger i = 0; i < count; i++) {
        id existing = ((id (*)(id, SEL, NSUInteger))objc_msgSend)(
            controllers, sel_registerName("objectAtIndex:"), (NSUInteger)i);
        if (g_settings_class && ((BOOL (*)(id, SEL, Class))objc_msgSend)(
                existing, sel_registerName("isKindOfClass:"), g_settings_class)) {
            ((id (*)(id, SEL, id, BOOL))objc_msgSend)(
                navigation, sel_registerName("popToViewController:animated:"), existing, YES);
            return;
        }
    }

    id controller = msg0((id)g_settings_class, "alloc");
    controller = ((id (*)(id, SEL, NSInteger))objc_msgSend)(
        controller, sel_registerName("initWithStyle:"), (NSInteger)2);
    ((void (*)(id, SEL, id, BOOL))objc_msgSend)(
        navigation, sel_registerName("pushViewController:animated:"), controller, YES);
    objc_release(controller);
}

static bool navigation_has_ad_block_item(id navigation_item) {
    id items = msg0(navigation_item, "rightBarButtonItems");
    NSInteger count = imsg0(items, "count");
    for (NSInteger i = 0; i < count; i++) {
        id item = ((id (*)(id, SEL, NSUInteger))objc_msgSend)(
            items, sel_registerName("objectAtIndex:"), (NSUInteger)i);
        if (bmsg1(msg0(item, "accessibilityIdentifier"), "isEqualToString:",
                  nsstr("TASAdBlockSettingsButton"))) return true;
    }
    return false;
}

static void add_ad_block_navigation_item(id controller) {
    id navigation_item = msg0(controller, "navigationItem");
    if (!navigation_item || navigation_has_ad_block_item(navigation_item)) return;
    id item = make_bar_button("Ad Block", controller, "tas_openAdBlockSettings");
    vmsg1(item, "setAccessibilityIdentifier:", nsstr("TASAdBlockSettingsButton"));
    id existing = msg0(navigation_item, "rightBarButtonItems");
    id items = existing ? msg0(existing, "mutableCopy") : msg0((id)objc_getClass("NSMutableArray"), "array");
    vmsg1(items, "addObject:", item);
    ((void (*)(id, SEL, id, BOOL))objc_msgSend)(
        navigation_item, sel_registerName("setRightBarButtonItems:animated:"), items, NO);
    if (existing) objc_release(items);
    objc_release(item);
}

static void app_settings_view_did_appear(id self, SEL command, BOOL animated) {
    if (g_app_settings_original_view_did_appear) {
        ((void (*)(id, SEL, BOOL))g_app_settings_original_view_did_appear)(self, command, animated);
    }
    add_ad_block_navigation_item(self);
}

static Class app_settings_class(void) {
    Class result = objc_getClass("_TtC6Twitch25AppSettingsViewController");
    if (!result) result = objc_getClass("Twitch.AppSettingsViewController");
    if (!result) result = objc_getClass("AppSettingsViewController");
    return result;
}

static bool install_app_settings_hook(void) {
    if (g_app_settings_hooked) return true;
    Class app_settings = app_settings_class();
    if (!app_settings) return false;
    class_addMethod(app_settings, sel_registerName("tas_openAdBlockSettings"),
                    (IMP)open_ad_block_settings, "v@:");
    SEL selector = sel_registerName("viewDidAppear:");
    Method method = class_getInstanceMethod(app_settings, selector);
    if (!method) return false;
    g_app_settings_original_view_did_appear = method_getImplementation(method);
    const char *types = method_getTypeEncoding(method);
    if (!class_addMethod(app_settings, selector, (IMP)app_settings_view_did_appear, types)) {
        method_setImplementation(method, (IMP)app_settings_view_did_appear);
    }
    g_app_settings_hooked = true;
    return true;
}

/*
 * AppSettingsViewController is implemented in Swift and may be registered
 * after this dylib's constructor. This base-class hook is a late-binding
 * fallback: it becomes active only when the real settings controller appears.
 */
static void view_controller_view_did_appear(id self, SEL command, BOOL animated) {
    if (g_view_controller_original_view_did_appear) {
        ((void (*)(id, SEL, BOOL))g_view_controller_original_view_did_appear)(self, command, animated);
    }
    if ((g_settings_class && ((BOOL (*)(id, SEL, Class))objc_msgSend)(
             self, sel_registerName("isKindOfClass:"), g_settings_class)) ||
        (g_log_class && ((BOOL (*)(id, SEL, Class))objc_msgSend)(
             self, sel_registerName("isKindOfClass:"), g_log_class))) return;
    Class actual_class = object_getClass(self);
    Class app_settings = app_settings_class();
    BOOL is_settings = app_settings ? ((BOOL (*)(id, SEL, Class))objc_msgSend)(
        self, sel_registerName("isKindOfClass:"), app_settings) : NO;
    const char *class_name = actual_class ? utf8(msg0((id)actual_class, "description")) : NULL;
    const char *title = utf8(msg0(self, "title"));
    bool likely_settings = is_settings ||
        (class_name && (strstr(class_name, "Settings") || strstr(class_name, "settings"))) ||
        (title && strcmp(title, "Settings") == 0);
    if (!likely_settings) return;
    if (actual_class) {
        class_addMethod(actual_class, sel_registerName("tas_openAdBlockSettings"),
                        (IMP)open_ad_block_settings, "v@:");
    }
    if (app_settings) install_app_settings_hook();
    add_ad_block_navigation_item(self);
}

static bool install_view_controller_fallback(void) {
    Class view_controller = objc_getClass("UIViewController");
    if (!view_controller) return false;
    Method method = class_getInstanceMethod(view_controller, sel_registerName("viewDidAppear:"));
    if (!method) return false;
    g_view_controller_original_view_did_appear = method_getImplementation(method);
    method_setImplementation(method, (IMP)view_controller_view_did_appear);
    return true;
}

static id visible_root_controller(void) {
    id application = msg0((id)objc_getClass("UIApplication"), "sharedApplication");
    id windows = msg0(application, "windows");
    NSInteger count = imsg0(windows, "count");
    id window = nil;
    for (NSInteger i = 0; i < count; i++) {
        id candidate = ((id (*)(id, SEL, NSUInteger))objc_msgSend)(
            windows, sel_registerName("objectAtIndex:"), (NSUInteger)i);
        if (bmsg0(candidate, "isKeyWindow")) {
            window = candidate;
            break;
        }
    }
    if (!window) window = msg0(windows, "firstObject");
    id controller = msg0(window, "rootViewController");
    for (int i = 0; controller && i < 12; i++) {
        id presented = msg0(controller, "presentedViewController");
        if (!presented) break;
        controller = presented;
    }
    return controller;
}

static void show_loaded_notice(id self, SEL command, id object) {
    (void)self;
    (void)command;
    (void)object;
    if (bmsg1(defaults(), "boolForKey:", nsstr(TAS_LOADED_NOTICE_KEY))) return;
    id controller = visible_root_controller();
    if (!controller) return;
    ((void (*)(id, SEL, BOOL, id))objc_msgSend)(
        defaults(), sel_registerName("setBool:forKey:"), YES, nsstr(TAS_LOADED_NOTICE_KEY));
    show_notice(controller, "VAFT loaded",
                "The VAFT module initialized. Open Profile → Settings, then tap Ad Block.");
}

static void retry_app_settings_hook(id self, SEL command, id notification) {
    (void)command;
    (void)notification;
    if (install_app_settings_hook()) {
        fprintf(stderr, "[TAS] AppSettingsViewController hook installed after application launch\n");
    }
    if (!bmsg1(defaults(), "boolForKey:", nsstr(TAS_LOADED_NOTICE_KEY))) {
        ((void (*)(id, SEL, SEL, id, double))objc_msgSend)(
            self, sel_registerName("performSelector:withObject:afterDelay:"),
            sel_registerName("tas_showLoadedNotice:"), nil, 1.25);
    }
}

static bool register_hook_retry_observer(void) {
    Class superclass = objc_getClass("NSObject");
    if (!superclass) return false;
    g_bootstrap_class = objc_allocateClassPair(superclass, "TASDiagnosticsBootstrap", 0);
    if (!g_bootstrap_class) g_bootstrap_class = objc_getClass("TASDiagnosticsBootstrap");
    if (!g_bootstrap_class) return false;
    if (!objc_getClass("TASDiagnosticsBootstrap")) {
        class_addMethod(g_bootstrap_class, sel_registerName("tas_retryAppSettingsHook:"),
                        (IMP)retry_app_settings_hook, "v@:@");
        class_addMethod(g_bootstrap_class, sel_registerName("tas_showLoadedNotice:"),
                        (IMP)show_loaded_notice, "v@:@");
        objc_registerClassPair(g_bootstrap_class);
    }
    g_bootstrap_observer = msg0((id)g_bootstrap_class, "new");
    if (!g_bootstrap_observer) return false;
    id center = msg0((id)objc_getClass("NSNotificationCenter"), "defaultCenter");
    if (!center) return false;
    SEL add_observer = sel_registerName("addObserver:selector:name:object:");
    ((void (*)(id, SEL, id, SEL, id, id))objc_msgSend)(
        center, add_observer, g_bootstrap_observer, sel_registerName("tas_retryAppSettingsHook:"),
        nsstr("UIApplicationDidFinishLaunchingNotification"), nil);
    ((void (*)(id, SEL, id, SEL, id, id))objc_msgSend)(
        center, add_observer, g_bootstrap_observer, sel_registerName("tas_retryAppSettingsHook:"),
        nsstr("UIApplicationDidBecomeActiveNotification"), nil);
    return true;
}

static bool register_settings_class(void) {
    Class superclass = objc_getClass("UITableViewController");
    if (!superclass) return false;
    g_settings_super_view_did_load = method_getImplementation(
        class_getInstanceMethod(superclass, sel_registerName("viewDidLoad")));
    g_settings_super_view_will_appear = method_getImplementation(
        class_getInstanceMethod(superclass, sel_registerName("viewWillAppear:")));
    g_settings_class = objc_allocateClassPair(superclass, "TASAdBlockSettingsViewController", 0);
    if (!g_settings_class) g_settings_class = objc_getClass("TASAdBlockSettingsViewController");
    if (!g_settings_class) return false;
    if (!objc_getClass("TASAdBlockSettingsViewController")) {
        class_addMethod(g_settings_class, sel_registerName("viewDidLoad"), (IMP)settings_view_did_load, "v@:");
        class_addMethod(g_settings_class, sel_registerName("viewWillAppear:"), (IMP)settings_view_will_appear, "v@:B");
        class_addMethod(g_settings_class, sel_registerName("numberOfSectionsInTableView:"), (IMP)settings_number_of_sections, "q@:@");
        class_addMethod(g_settings_class, sel_registerName("tableView:numberOfRowsInSection:"), (IMP)settings_rows_in_section, "q@:@q");
        class_addMethod(g_settings_class, sel_registerName("tableView:titleForHeaderInSection:"), (IMP)settings_header, "@@:@q");
        class_addMethod(g_settings_class, sel_registerName("tableView:titleForFooterInSection:"), (IMP)settings_footer, "@@:@q");
        class_addMethod(g_settings_class, sel_registerName("tableView:cellForRowAtIndexPath:"), (IMP)settings_cell, "@@:@@");
        class_addMethod(g_settings_class, sel_registerName("tableView:didSelectRowAtIndexPath:"), (IMP)settings_did_select, "v@:@@");
        class_addMethod(g_settings_class, sel_registerName("tas_diagnosticsSwitchChanged:"), (IMP)settings_switch_changed, "v@:@");
        objc_registerClassPair(g_settings_class);
    }
    return true;
}

static bool register_log_class(void) {
    Class superclass = objc_getClass("UIViewController");
    if (!superclass) return false;
    g_log_super_view_did_load = method_getImplementation(
        class_getInstanceMethod(superclass, sel_registerName("viewDidLoad")));
    g_log_super_view_will_appear = method_getImplementation(
        class_getInstanceMethod(superclass, sel_registerName("viewWillAppear:")));
    g_log_class = objc_allocateClassPair(superclass, "TASDiagnosticLogViewController", 0);
    if (!g_log_class) g_log_class = objc_getClass("TASDiagnosticLogViewController");
    if (!g_log_class) return false;
    if (!objc_getClass("TASDiagnosticLogViewController")) {
        class_addMethod(g_log_class, sel_registerName("viewDidLoad"), (IMP)log_view_did_load, "v@:");
        class_addMethod(g_log_class, sel_registerName("viewWillAppear:"), (IMP)log_view_will_appear, "v@:B");
        class_addMethod(g_log_class, sel_registerName("tas_copyDiagnosticReport"), (IMP)log_copy, "v@:");
        objc_registerClassPair(g_log_class);
    }
    return true;
}

void tas_diagnostics_initialize(void) {
    bool settings_registered = register_settings_class();
    bool log_registered = register_log_class();
    bool fallback = install_view_controller_fallback();
    bool retry_registered = register_hook_retry_observer();
    bool hooked = settings_registered && log_registered && install_app_settings_hook();
    fprintf(stderr, "[TAS] diagnostics UI %s (AppSettings hook %s; fallback %s; retry %s)\n",
            settings_registered && log_registered ? "registered" : "unavailable",
            hooked ? "installed" : "deferred",
            fallback ? "installed" : "unavailable",
            retry_registered ? "registered" : "unavailable");
    if (retry_registered && !bmsg1(defaults(), "boolForKey:", nsstr(TAS_LOADED_NOTICE_KEY))) {
        ((void (*)(id, SEL, SEL, id, double))objc_msgSend)(
            g_bootstrap_observer, sel_registerName("performSelector:withObject:afterDelay:"),
            sel_registerName("tas_showLoadedNotice:"), nil, 1.25);
    }
    tas_diag_log("PORT_LOADED", "VAFT v24 iOS port initialized; diagnostics UI registration attempted");
}
