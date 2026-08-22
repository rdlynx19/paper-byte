#include "FileServer.h"
#include <SD.h>
#include <Update.h>

FileServer g_file_server;

// Escapes text for safe placement inside HTML (both element text and
// attribute values) — filenames are user-controlled (whatever someone named
// their epub) and real book titles routinely contain apostrophes, so this
// isn't an edge case: without it, a name like "Editor's Choice.epub" spliced
// raw into `value='...'` breaks the attribute (and any earlier version of
// this page that put filenames straight into onclick="..." JS strings too).
static String html_escape(const String &s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

// Percent-encodes text for safe placement inside a URL query string (href
// links, not form fields — forms encode their own submitted values).
static String url_encode(const String &s) {
    static const char *hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        unsigned char c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// Rejects anything that isn't a bare filename — no authentication beyond the
// AP's own WPA2 password protects this server, so it's worth not trusting
// `file`/`dir` arguments enough to let them escape the SD card's own tree
// via ".." or walk into a different path than the one shown to the user.
static bool is_safe_filename(const String &name) {
    if (name.length() == 0) return false;
    return name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf("..") < 0;
}

static bool is_safe_dir(const String &dir) {
    if (dir.length() == 0 || dir[0] != '/') return false;
    return dir.indexOf("..") < 0;
}

// Listing filter — only epub/jpg files are ever something you'd want to
// manage here; everything else on the card (position/settings files, the
// .bin cover-art cache, etc.) is an internal implementation detail. Doesn't
// apply to directories, which always need to stay visible/navigable
// regardless of what's inside them.
static bool is_listed_file(const String &name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".epub") || lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

static String join_path(const String &dir, const String &name) {
    return dir == "/" ? ("/" + name) : (dir + "/" + name);
}

static String parent_of(const String &dir) {
    if (dir == "/") return "/";
    int slash = dir.lastIndexOf('/');
    return slash <= 0 ? "/" : dir.substring(0, slash);
}

bool FileServer::begin() {
    if (!SD.exists(EPUB_DIR)) SD.mkdir(EPUB_DIR);

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) return false;

    m_server.on("/", HTTP_GET, [this]() { handle_root(); });
    m_server.on("/download", HTTP_GET, [this]() { handle_download(); });
    m_server.on("/delete", HTTP_POST, [this]() { handle_delete(); });
    m_server.on("/upload", HTTP_POST, [this]() { handle_upload_done(); }, [this]() { handle_upload_data(); });
    m_server.on("/update", HTTP_POST, [this]() { handle_ota_done(); }, [this]() { handle_ota_data(); });
    m_server.begin();
    return true;
}

void FileServer::update() {
    m_server.handleClient();
}

void FileServer::stop() {
    m_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

void FileServer::handle_root() {
    String dir = m_server.hasArg("dir") ? m_server.arg("dir") : "/";
    if (!is_safe_dir(dir)) dir = "/";

    String dir_url = url_encode(dir);
    String html =
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Paper-Byte</title></head><body>"
        "<h2>Paper-Byte &mdash; " + html_escape(dir) + "</h2>"
        "<form method='POST' action='/upload?dir=" + dir_url + "' enctype='multipart/form-data'>"
        "<input type='file' name='file'> <input type='submit' value='Upload here'></form>"
        "<hr>"
        "<details><summary>Firmware update</summary>"
        "<form method='POST' action='/update' enctype='multipart/form-data' "
        "onsubmit=\"return confirm('Flash this .bin as new firmware and reboot? "
        "Make sure it was built for this board.');\">"
        "<input type='file' name='firmware' accept='.bin'> "
        "<input type='submit' value='Flash firmware'></form></details><hr>";

    if (dir != "/") {
        html += "<p><a href='/?dir=" + url_encode(parent_of(dir)) + "'>.. (up)</a></p>";
    }

    html += "<ul>";
    File d = SD.open(dir);
    if (d && d.isDirectory()) {
        File f;
        while ((f = d.openNextFile())) {
            String name = f.name();
            bool is_dir  = f.isDirectory();
            size_t size  = f.size();
            f.close();

            if (!is_dir && !is_listed_file(name)) continue;

            String child     = join_path(dir, name);
            String child_url = url_encode(child);
            String name_html = html_escape(name);

            if (is_dir) {
                html += "<li>[DIR] <a href='/?dir=" + child_url + "'>" + name_html + "</a></li>";
            } else {
                html += "<li>" + name_html + " (" + String(size / 1024) + " KB) &mdash; "
                        "<a href='/download?dir=" + dir_url + "&file=" + url_encode(name) + "'>download</a> &mdash; "
                        "<form style='display:inline' method='POST' action='/delete' "
                        "onsubmit=\"return confirm('Delete this file?');\">"
                        "<input type='hidden' name='dir' value='" + html_escape(dir) + "'>"
                        "<input type='hidden' name='file' value='" + name_html + "'>"
                        "<input type='submit' value='delete'></form></li>";
            }
        }
    }
    if (d) d.close();
    html += "</ul></body></html>";

    m_server.sendHeader("Cache-Control", "no-store");
    m_server.send(200, "text/html", html);
}

void FileServer::handle_download() {
    String dir  = m_server.hasArg("dir")  ? m_server.arg("dir")  : "/";
    String name = m_server.hasArg("file") ? m_server.arg("file") : "";
    if (!is_safe_dir(dir) || !is_safe_filename(name)) {
        m_server.send(400, "text/plain", "bad request");
        return;
    }

    File f = SD.open(join_path(dir, name), FILE_READ);
    if (!f || f.isDirectory()) {
        m_server.send(404, "text/plain", "not found");
        return;
    }

    m_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    m_server.streamFile(f, "application/octet-stream");
    f.close();
}

void FileServer::handle_delete() {
    String dir  = m_server.hasArg("dir")  ? m_server.arg("dir")  : "/";
    String name = m_server.hasArg("file") ? m_server.arg("file") : "";
    if (!is_safe_dir(dir) || !is_safe_filename(name)) {
        m_server.send(400, "text/plain", "bad request");
        return;
    }

    bool ok = SD.remove(join_path(dir, name));
    if (!ok) {
        m_server.send(500, "text/plain", "delete failed");
        return;
    }

    m_server.sendHeader("Cache-Control", "no-store");
    m_server.sendHeader("Location", "/?dir=" + url_encode(dir));
    m_server.send(303);
}

void FileServer::handle_upload_done() {
    String dir = m_server.hasArg("dir") ? m_server.arg("dir") : "/";
    if (!is_safe_dir(dir)) dir = "/";
    m_server.sendHeader("Cache-Control", "no-store");
    m_server.sendHeader("Location", "/?dir=" + url_encode(dir));
    m_server.send(303);
}

void FileServer::handle_upload_data() {
    static File upload_file;
    HTTPUpload &upload = m_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String dir = m_server.hasArg("dir") ? m_server.arg("dir") : "/";
        if (!is_safe_dir(dir) || !is_safe_filename(upload.filename)) return;
        upload_file = SD.open(join_path(dir, upload.filename), FILE_WRITE);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (upload_file) upload_file.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
        if (upload_file) upload_file.close();
    }
}

// Responds to the /update POST once the whole .bin has streamed through
// handle_ota_data(). Only reboots on success — a failed flash leaves the
// currently-running firmware untouched and in charge, so a bad upload is a
// "try again" situation, not a bricked device.
void FileServer::handle_ota_done() {
    bool ok = !Update.hasError();
    m_server.sendHeader("Connection", "close");
    m_server.sendHeader("Cache-Control", "no-store");
    m_server.send(200, "text/plain",
                  ok ? "Update OK, rebooting..." : "Update FAILED, device not rebooted (see serial log)");
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

// Streams the uploaded .bin straight into the inactive OTA partition via
// the ESP32 core's Update library — no SD card or extra buffering involved.
void FileServer::handle_ota_data() {
    HTTPUpload &upload = m_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA: starting flash of %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("OTA: flashed %u bytes, rebooting into it next\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        Serial.println("OTA: upload aborted");
    }
}
