#include "onvif.h"
#include "ptz.h"

#include <math.h>
#include <stdarg.h>

IMPORT_STR(.rodata, "../res/onvif/capabilities.xml", capabilitiesxml);
extern const char capabilitiesxml[];
IMPORT_STR(.rodata, "../res/onvif/deviceinfo.xml", deviceinfoxml);
extern const char deviceinfoxml[];
IMPORT_STR(.rodata, "../res/onvif/discovery.xml", discoveryxml);
extern const char discoveryxml[];
IMPORT_STR(.rodata, "../res/onvif/mediaprofile.xml", mediaprofilexml);
extern const char mediaprofilexml[];
IMPORT_STR(.rodata, "../res/onvif/mediaprofiles.xml", mediaprofilesxml);
extern const char mediaprofilesxml[];
IMPORT_STR(.rodata, "../res/onvif/snapshot.xml", snapshotxml);
extern const char snapshotxml[];
IMPORT_STR(.rodata, "../res/onvif/stream.xml", streamxml);
extern const char streamxml[];
IMPORT_STR(.rodata, "../res/onvif/systemtime.xml", systemtimexml);
extern const char systemtimexml[];
IMPORT_STR(.rodata, "../res/onvif/videosources.xml", videosourcesxml);
extern const char videosourcesxml[];

const char onvifgood[] = "HTTP/1.1 200 OK\r\n" \
                         "Content-Type: application/soap+xml; charset=utf-8\r\n" \
                         "Connection: close\r\n" \
                         "\r\n";

extern NetInfo netinfo;
pthread_t onvifPid = 0;

int start_onvif(void) {
    pthread_attr_t thread_attr;
    int ret;

    pthread_attr_init(&thread_attr);
    size_t stacksize;
    pthread_attr_getstacksize(&thread_attr, &stacksize);
    size_t new_stacksize = 16 * 1024;
    if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
        HAL_DANGER("onvif", "Can't set stack size %zu\n", new_stacksize);
    ret = pthread_create(&onvifPid, &thread_attr, onvif_thread, NULL);
    if (pthread_attr_setstacksize(&thread_attr, stacksize))
        HAL_DANGER("onvif", "Can't set stack size %zu\n", stacksize);
    pthread_attr_destroy(&thread_attr);

    if (ret) {
        HAL_DANGER("onvif", "Starting the discovery thread failed: %s\n",
            strerror(ret));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void stop_onvif(void) {
    pthread_join(onvifPid, NULL);
}

void *onvif_thread(void *arg) {
    (void)arg;
    char request[4096], response[4096];
    int servfd, reqLen;
    struct sockaddr_in servaddr, clntaddr;
    socklen_t clntsz;

    if ((servfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        HAL_DANGER("onvif", "Failed to create socket!\n");
        return (void*)EXIT_FAILURE;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(3702);

    struct ip_mreq group = {
        .imr_multiaddr.s_addr = inet_addr("239.255.255.250"),
        .imr_interface.s_addr = inet_addr(netinfo.ipaddr[0])
    };

    if (bind(servfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        HAL_DANGER("onvif", "Failed to bind socket!\n");
        close(servfd);
        return (void*)EXIT_FAILURE;
    }

    if (setsockopt(servfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group)) < 0){
        close(servfd);
        return (void*)EXIT_FAILURE;
    }

    while (keepRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(servfd, &readfds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(servfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            HAL_DANGER("onvif", "Polling using select failed: %s\n", strerror(errno));
            continue;
        } else if (!ret) continue;

        clntsz = sizeof(clntaddr);
        if ((reqLen = recvfrom(servfd, request, sizeof(request) - 1, 0, (struct sockaddr *)&clntaddr, &clntsz)) < 0)
            continue;

        request[reqLen] = '\0';
#ifdef DEBUG_ONVIF
        HAL_INFO("onvif", "Received message: %s\n", msgbuf);
#endif

        if (!CONTAINS(request, "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe"))
            continue;

        char device_name[64], device_uuid[64], device_url[128], msgid[100] = {0};
        {
            char uuid[37];
            uuid_generate(uuid);
            snprintf(device_uuid, sizeof(device_uuid), "urn:uuid:%s", uuid);
        }
        snprintf(device_name, sizeof(device_name), "Divinus");
        snprintf(device_url, sizeof(device_url), "http://%s:%d/onvif/device_service",
            netinfo.ipaddr[0], app_config.web_port);
    
        char *msgid_init = strstr(request, "MessageID>");
        if (msgid_init) {
            msgid_init += 10;
            char *msgid_end = strstr(msgid_init, "<");
            if (msgid_end && (msgid_end - msgid_init) < sizeof(msgid)) {
                strncpy(msgid, msgid_init, msgid_end - msgid_init);
                msgid[msgid_end - msgid_init] = '\0';
            }
        }
        
        snprintf(response, sizeof(response), discoveryxml,
            device_uuid, msgid, device_uuid, device_name, device_url);
    
        HAL_INFO("onvif", "Sending discovery response to %s:%d\n", 
                 inet_ntoa(clntaddr.sin_addr), ntohs(clntaddr.sin_port));
    
        if (sendto(servfd, response, strlen(response), 0, (struct sockaddr *)&clntaddr, clntsz) < 0)
            HAL_WARNING("onvif", "Failed to send discovery response: %s\n", strerror(errno));
    }

    close(servfd);
    return (void*)EXIT_SUCCESS;
}

char* onvif_extract_soap_action(const char* soap_data) {
    static char action[128];
    char *action_end;
    
    char *body_start = strstr(soap_data, "Body");
    if (!body_start) return NULL;
    
    body_start = strchr(body_start, '>');
    if (!body_start) return NULL;
    body_start++;

    while (*body_start && isspace(*body_start)) body_start++;
    
    if (*body_start != '<') return NULL;
    body_start++;

    action_end = body_start;
    while (*action_end && !isspace((unsigned char)*action_end) &&
           *action_end != '>' && *action_end != '/') action_end++;
    if (action_end == body_start || !*action_end) return NULL;
    
    int action_len = action_end - body_start;
    if (action_len >= sizeof(action)) action_len = sizeof(action) - 1;
    
    strncpy(action, body_start, action_len);
    action[action_len] = '\0';
    char *namespace = strrchr(action, ':');
    if (namespace)
        memmove(action, namespace + 1, strlen(namespace + 1) + 1);
    
    return action;
}

bool onvif_validate_soap_auth(const char *soap_data) {
    const char *created_tag = "Created", *digest_tag = "PasswordDigest", *nonce_tag = "<Nonce", 
        *pass_tag = "<Password", *type_attr = "Type=\"", *user_tag = "<Username>";
    char *pos, *end, *start;
    char digest = 0, created[64], nonce[64], pass[64], user[64];

    if (!(start = strstr(soap_data, user_tag)) ||
        !(start += strlen(user_tag))) return false;
    if (!(end = strstr(start, "</Username>"))) return false;
    if ((size_t)(end - start) >= sizeof(user)) return false;
    memcpy(user, start, end - start);
    user[end - start] = '\0';

    if (!EQUALS(user, app_config.onvif_auth_user)) {
        HAL_WARNING("onvif", "Invalid username: %s\n", user);
        return false;
    }

    start = strstr(soap_data, pass_tag);
    if (!start) return false;
    start += strlen(pass_tag);

    end = strchr(start, '>');
    if (!end) return false;

    pos = strstr(start, type_attr);
    if (pos && pos < end) {
        char *digest_pos;

        pos += strlen(type_attr);
        digest_pos = strstr(pos, digest_tag);
        if (digest_pos && digest_pos < end)
            digest = 1;
    }

    start = end + 1;
    end = strstr(start, "</Password>");
    if (!end) return false;
    if ((size_t)(end - start) >= sizeof(pass)) return false;
    memcpy(pass, start, end - start);
    pass[end - start] = '\0';

    if (digest) {
        unsigned char digest_comp[SHA1_DIGEST_SIZE] = {0};
        char nonce_dec[64], pass_dec[64];
        sha1_context ctx;

        if (!(start = strstr(soap_data, nonce_tag)) ||
            !(start = strchr(start, '>'))) return false;
        start++;
        if (!(end = strstr(start, "</Nonce>"))) return false;
        if ((size_t)(end - start) >= sizeof(nonce)) return false;
        memcpy(nonce, start, end - start);
        nonce[end - start] = '\0';

        if (!(start = strstr(soap_data, created_tag)) ||
            !(start = strchr(start, '>'))) return false;
        start++;
        if (!(end = strstr(start, "</Created>"))) return false;
        if ((size_t)(end - start) >= sizeof(created)) return false;
        memcpy(created, start, end - start);
        created[end - start] = '\0';

        int nonce_len = base64_decode(nonce_dec, nonce, sizeof(nonce_dec));
        if (nonce_len < 0) return false;

        sha1_init(&ctx);
        sha1_update(&ctx, (unsigned char *)nonce_dec, nonce_len - 1);
        sha1_update(&ctx, (unsigned char *)created, strlen(created));
        sha1_update(&ctx, (unsigned char *)app_config.onvif_auth_pass, strlen(app_config.onvif_auth_pass));
        sha1_final(digest_comp, &ctx);

        int pass_len = base64_encode(pass_dec, digest_comp, SHA1_DIGEST_SIZE);
        if (pass_len < 0) return false;
        pass_dec[pass_len] = '\0';

        bool valid = !memcmp(pass, pass_dec, pass_len);
        if (valid)
            HAL_INFO("onvif", "Valid password digest!\n");
        else
            HAL_WARNING("onvif", "Invalid password digest!\n");
        return valid;
    } else {
        bool valid = EQUALS(pass, app_config.onvif_auth_pass);
        if (valid)
            HAL_INFO("onvif", "Valid password provided!\n");
        else
            HAL_WARNING("onvif", "Invalid password provided!\n");
        return valid;
    }
}

void onvif_respond_capabilities(char *response, int *respLen) {
    if (!response || !respLen) return;

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        capabilitiesxml,
        netinfo.ipaddr[0], app_config.web_port,    // Analytics
        netinfo.ipaddr[0], app_config.web_port,    // Device
        netinfo.ipaddr[0], app_config.web_port,    // Events
        netinfo.ipaddr[0], app_config.web_port,    // Imaging
        netinfo.ipaddr[0], app_config.web_port,    // Media
        netinfo.ipaddr[0], app_config.web_port);   // PTZ
}

void onvif_respond_deviceinfo(char *response, int *respLen) {
    if (!response || !respLen) return;

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        deviceinfoxml,
        "OpenIPC", "IP Camera", "1.0", "To be replaced", chip);
}

void onvif_respond_mediaprofiles(char *response, int *respLen) {
    if (!response || !respLen) return;

    char profile[4096];
    char profileCnt = 0;
    int profileLen = 0;

    if (app_config.mp4_enable) {
        profileLen += sprintf(&profile[profileLen], mediaprofilexml,
            "profile_1", "MainStream",
            profileCnt + 1, profileCnt + 1,
            app_config.mp4_height, app_config.mp4_width,
            profileCnt + 1, profileCnt + 1,
            app_config.mp4_codecH265 ? "H265" : "H264",
            app_config.mp4_width, app_config.mp4_height,
            app_config.mp4_fps, app_config.mp4_bitrate);
        profileCnt++;
    }

    if (app_config.mjpeg_enable) {
        profileLen += sprintf(&profile[profileLen], mediaprofilexml,
            "profile_2", "SubStream",
            profileCnt + 1, profileCnt + 1,
            app_config.mjpeg_height, app_config.mjpeg_width,
            profileCnt + 1, profileCnt + 1,
            "JPEG", app_config.mjpeg_width, app_config.mjpeg_height,
            app_config.mjpeg_fps, app_config.mjpeg_bitrate);
        profileCnt++;
    }

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        mediaprofilesxml,
        profile);
}

void onvif_respond_snapshot(char *response, int *respLen) {
    if (!response || !respLen) return;

    char snapshot_url[256];

    if (app_config.web_enable_auth && 
        *app_config.web_auth_user && *app_config.web_auth_pass) {
        char user[96], pass[96];
        escape_url(user, app_config.web_auth_user, sizeof(user));
        escape_url(pass, app_config.web_auth_pass, sizeof(pass));
        snprintf(snapshot_url, sizeof(snapshot_url), "http://%s:%s@%s:%d/image.jpg",
            user, pass, netinfo.ipaddr[0], app_config.web_port);
    } else
        snprintf(snapshot_url, sizeof(snapshot_url), "http://%s:%d/image.jpg",
            netinfo.ipaddr[0], app_config.web_port);

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        snapshotxml,
        snapshot_url);
}

void onvif_respond_stream(char *response, int *respLen) {
    if (!response || !respLen) return;

    char stream_url[256];

    if (app_config.rtsp_enable_auth && 
        *app_config.rtsp_auth_user && *app_config.rtsp_auth_pass) {
        char user[96], pass[96];
        escape_url(user, app_config.rtsp_auth_user, sizeof(user));
        escape_url(pass, app_config.rtsp_auth_pass, sizeof(pass));
        snprintf(stream_url, sizeof(stream_url), "rtsp://%s:%s@%s:%d/stream=0",
            user, pass, netinfo.ipaddr[0], app_config.rtsp_port);
    } else
        snprintf(stream_url, sizeof(stream_url), "rtsp://%s:%d/stream=0",
            netinfo.ipaddr[0], app_config.rtsp_port);

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        streamxml,
        stream_url);
}

void onvif_respond_systemtime(char *response, int *respLen) {
    if (!response || !respLen) return;

    time_t now;
    struct tm *tm_info;

    time(&now);
    tm_info = gmtime(&now);

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        systemtimexml,
        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
        tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday);
}

void onvif_respond_videosources(char *response, int *respLen) {
    if (!response || !respLen) return;

    int width = app_config.mp4_enable ?
        app_config.mp4_width : app_config.mjpeg_width;
    int height = app_config.mp4_enable ?
        app_config.mp4_height : app_config.mjpeg_height;
    int framerate = app_config.mp4_enable ?
        app_config.mp4_fps : app_config.mjpeg_fps;

    int maxLen = *respLen;
    int headerLen = strlen(onvifgood);
    memcpy(response, onvifgood, headerLen);
    *respLen = headerLen;

    *respLen += snprintf(response + headerLen, maxLen - headerLen,
        videosourcesxml,
        framerate, width, height);
}

static int onvif_soap_write(char *response, int *respLen, const char *format, ...) {
    va_list args;
    int header_len, body_len;

    if (!response || !respLen || *respLen <= 0 || !format) return -EINVAL;
    header_len = (int)strlen(onvifgood);
    if (header_len >= *respLen) return -EOVERFLOW;
    memcpy(response, onvifgood, (size_t)header_len);
    va_start(args, format);
    body_len = vsnprintf(response + header_len, (size_t)(*respLen - header_len),
        format, args);
    va_end(args);
    if (body_len < 0 || body_len >= *respLen - header_len) return -EOVERFLOW;
    *respLen = header_len + body_len;
    return 0;
}

static int onvif_xml_attribute(const char *payload, const char *element,
    const char *attribute, double *value) {
    const char *start, *end, *found, *number;
    char pattern[32], quote, *tail;
    double parsed;

    if (!payload || !element || !attribute || !value) return -EINVAL;
    start = strstr(payload, element);
    if (!start || !(end = strchr(start, '>'))) return -EINVAL;
    snprintf(pattern, sizeof(pattern), "%s=", attribute);
    found = strstr(start, pattern);
    if (!found || found >= end) return -EINVAL;
    number = found + strlen(pattern);
    quote = *number;
    if (quote != '\'' && quote != '"') return -EINVAL;
    errno = 0;
    parsed = strtod(number + 1, &tail);
    if (errno || tail == number + 1 || *tail != quote || tail >= end)
        return -EINVAL;
    *value = parsed;
    return 0;
}

static int onvif_xml_text(const char *payload, const char *element,
    char *value, size_t value_size) {
    const char *cursor;
    size_t element_len;

    if (!payload || !element || !value || value_size < 2) return -EINVAL;
    element_len = strlen(element);
    cursor = payload;
    while ((cursor = strchr(cursor, '<'))) {
        const char *name = cursor + 1, *open_end, *text_end, *local;
        size_t text_len;

        if (*name == '/') { cursor = name + 1; continue; }
        open_end = strchr(name, '>');
        if (!open_end) return -EINVAL;
        local = name;
        for (const char *scan = name; scan < open_end; ++scan) {
            if (*scan == ':') local = scan + 1;
            if (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n')
                break;
        }
        if (!strncmp(local, element, element_len) &&
            (local[element_len] == '>' || local[element_len] == ' ' ||
             local[element_len] == '\t' || local[element_len] == '\r' ||
             local[element_len] == '\n')) {
            text_end = strchr(open_end + 1, '<');
            if (!text_end) return -EINVAL;
            text_len = (size_t)(text_end - (open_end + 1));
            if (!text_len || text_len >= value_size) return -EINVAL;
            memcpy(value, open_end + 1, text_len);
            value[text_len] = '\0';
            return 0;
        }
        cursor = open_end + 1;
    }
    return -ENOENT;
}

static int onvif_ptz_fault(char *response, int *respLen, int error) {
    return onvif_soap_write(response, respLen,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
        "<s:Body><s:Fault><s:Code><s:Value>s:Receiver</s:Value></s:Code>"
        "<s:Reason><s:Text xml:lang=\"en\">PTZ backend error %d</s:Text>"
        "</s:Reason></s:Fault></s:Body></s:Envelope>", -error);
}

static int onvif_ptz_empty(char *response, int *respLen, const char *action) {
    return onvif_soap_write(response, respLen,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
        "<s:Body><tptz:%sResponse/></s:Body></s:Envelope>", action);
}

static double onvif_normalize(int value, int minimum, int maximum) {
    if (maximum <= minimum) return 0.0;
    return ((double)(value - minimum) * 2.0 / (double)(maximum - minimum)) - 1.0;
}

static int onvif_denormalize(double value, int minimum, int maximum) {
    double bounded = value < -1.0 ? -1.0 : value > 1.0 ? 1.0 : value;
    return minimum + (int)lround((bounded + 1.0) * (maximum - minimum) / 2.0);
}

static int onvif_clamp_target(int value, int minimum, int maximum) {
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

/*
 * TranslationSpaceFov is a view-relative request, not a request to traverse
 * the complete mechanical axis.  Keep one normalized FOV unit aligned with
 * the existing PTZ velocity scale; mapping it to the whole raw range makes
 * Frigate calibration take ~13 s at x/y=1 and violates its <=2 s/unit model.
 */
#define ONVIF_FOV_PAN_STEPS 64
#define ONVIF_FOV_TILT_STEPS 24

int onvif_respond_ptz(const char *action, const char *payload,
    char *response, int *respLen) {
    struct ptz_status status;
    int rc;

    if (!action || !response || !respLen) return 0;
    if (EQUALS(action, "GetServiceCapabilities")) {
        onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
            "<s:Body><tptz:GetServiceCapabilitiesResponse>"
            "<tptz:Capabilities EFlip=\"false\" Reverse=\"false\" "
            "GetCompatibleConfigurations=\"true\" MoveStatus=\"true\" "
            "StatusPosition=\"true\"/></tptz:GetServiceCapabilitiesResponse>"
            "</s:Body></s:Envelope>");
        return 1;
    }
    if (EQUALS(action, "GetNodes")) {
        onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
            "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<s:Body><tptz:GetNodesResponse><tptz:PTZNode token=\"fh8626\">"
            "<tt:Name>FH8626 PTZ</tt:Name><tt:SupportedPTZSpaces>"
            "<tt:AbsolutePanTiltPositionSpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:AbsolutePanTiltPositionSpace>"
            "<tt:RelativePanTiltTranslationSpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:RelativePanTiltTranslationSpace>"
            "<tt:ContinuousPanTiltVelocitySpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:ContinuousPanTiltVelocitySpace>"
            "</tt:SupportedPTZSpaces><tt:MaximumNumberOfPresets>8</tt:MaximumNumberOfPresets><tt:HomeSupported>true</tt:HomeSupported>"
            "</tptz:PTZNode></tptz:GetNodesResponse></s:Body></s:Envelope>");
        return 1;
    }
    if (EQUALS(action, "GetConfigurations") || EQUALS(action, "GetConfiguration") ||
        EQUALS(action, "GetCompatibleConfigurations")) {
        const char *name = EQUALS(action, "GetConfigurations") ? "GetConfigurations" :
            EQUALS(action, "GetConfiguration") ? "GetConfiguration" :
            "GetCompatibleConfigurations";
        onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<s:Body><tptz:%sResponse><tptz:PTZConfiguration token=\"fh8626-config\"><tt:Name>FH8626 PTZ</tt:Name><tt:UseCount>1</tt:UseCount><tt:NodeToken>fh8626</tt:NodeToken><tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:DefaultRelativePanTiltTranslationSpace><tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout></tptz:PTZConfiguration></tptz:%sResponse></s:Body></s:Envelope>",
            name, name);
        return 1;
    }
    if (EQUALS(action, "GetConfigurationOptions")) {
        onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<s:Body><tptz:GetConfigurationOptionsResponse><tptz:PTZConfigurationOptions><tt:Spaces>"
            "<tt:AbsolutePanTiltPositionSpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:AbsolutePanTiltPositionSpace>"
            "<tt:RelativePanTiltTranslationSpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:RelativePanTiltTranslationSpace>"
            "<tt:ContinuousPanTiltVelocitySpace><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI><tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange><tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange></tt:ContinuousPanTiltVelocitySpace>"
            "</tt:Spaces><tt:PTZTimeout><tt:Min>PT0.1S</tt:Min><tt:Max>PT30S</tt:Max></tt:PTZTimeout></tptz:PTZConfigurationOptions></tptz:GetConfigurationOptionsResponse></s:Body></s:Envelope>");
        return 1;
    }
    rc = ptz_status_read(&status);
    if (rc) {
        onvif_ptz_fault(response, respLen, rc);
        return 1;
    }
    if (EQUALS(action, "GetStatus")) {
        onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<s:Body><tptz:GetStatusResponse><tptz:PTZStatus><tt:Position><tt:PanTilt x=\"%.6f\" y=\"%.6f\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace\"/></tt:Position>"
            "<tt:MoveStatus><tt:PanTilt>%s</tt:PanTilt></tt:MoveStatus><tt:Error></tt:Error></tptz:PTZStatus></tptz:GetStatusResponse></s:Body></s:Envelope>",
            onvif_normalize(status.pan, status.pan_min, status.pan_max),
            onvif_normalize(status.tilt, status.tilt_min, status.tilt_max),
            status.busy ? "MOVING" : "IDLE");
        return 1;
    }
    if (EQUALS(action, "GetPresets")) {
        struct ptz_preset presets[PTZ_MAX_PRESETS];
        char entries[2048];
        size_t used = 0;
        int count = ptz_presets_read(presets, PTZ_MAX_PRESETS);

        if (count < 0) rc = count;
        else {
            entries[0] = '\0';
            for (int index = 0; index < count; ++index) {
                int length = snprintf(entries + used, sizeof(entries) - used,
                    "<tptz:Preset token=\"%s\"><tt:Name>%s</tt:Name>"
                    "<tt:PTZPosition><tt:PanTilt x=\"%.6f\" y=\"%.6f\" "
                    "space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace\"/>"
                    "</tt:PTZPosition></tptz:Preset>",
                    presets[index].token, presets[index].token,
                    onvif_normalize(presets[index].pan, status.pan_min, status.pan_max),
                    onvif_normalize(presets[index].tilt, status.tilt_min, status.tilt_max));
                if (length < 0 || (size_t)length >= sizeof(entries) - used) {
                    rc = -EOVERFLOW;
                    break;
                }
                used += (size_t)length;
            }
        }
        if (rc) onvif_ptz_fault(response, respLen, rc);
        else onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
            "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
            "<s:Body><tptz:GetPresetsResponse>%s</tptz:GetPresetsResponse>"
            "</s:Body></s:Envelope>", entries);
        return 1;
    }
    if (EQUALS(action, "SetPreset")) {
        char requested[32] = "", token[32];
        int token_rc = onvif_xml_text(payload, "PresetToken", requested,
            sizeof(requested));
        if (token_rc == -ENOENT)
            token_rc = onvif_xml_text(payload, "PresetName", requested,
                sizeof(requested));
        rc = ptz_preset_set(token_rc ? NULL : requested, token, sizeof(token));
        if (rc) onvif_ptz_fault(response, respLen, rc);
        else onvif_soap_write(response, respLen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
            "<s:Body><tptz:SetPresetResponse><tptz:PresetToken>%s"
            "</tptz:PresetToken></tptz:SetPresetResponse></s:Body></s:Envelope>", token);
        return 1;
    }
    if (EQUALS(action, "GotoPreset") || EQUALS(action, "RemovePreset")) {
        char token[32];
        rc = onvif_xml_text(payload, "PresetToken", token, sizeof(token));
        if (!rc) rc = EQUALS(action, "GotoPreset") ?
            ptz_preset_goto(token) : ptz_preset_remove(token);
        if (rc) onvif_ptz_fault(response, respLen, rc);
        else onvif_ptz_empty(response, respLen, action);
        return 1;
    }
    if (EQUALS(action, "GotoHomePosition")) rc = ptz_home();
    else if (EQUALS(action, "AbsoluteMove")) {
        double pan, tilt;
        if (onvif_xml_attribute(payload, "PanTilt", "x", &pan) ||
            onvif_xml_attribute(payload, "PanTilt", "y", &tilt)) rc = -EINVAL;
        else rc = ptz_move_absolute(
            onvif_denormalize(pan, status.pan_min, status.pan_max),
            onvif_denormalize(tilt, status.tilt_min, status.tilt_max));
    } else if (EQUALS(action, "RelativeMove")) {
        double pan, tilt;
        if (onvif_xml_attribute(payload, "PanTilt", "x", &pan) ||
            onvif_xml_attribute(payload, "PanTilt", "y", &tilt)) rc = -EINVAL;
        else if (!isfinite(pan) || !isfinite(tilt) ||
            status.pan < status.pan_min || status.pan > status.pan_max ||
            status.tilt < status.tilt_min || status.tilt > status.tilt_max)
            rc = -ERANGE;
        else {
            int requested_pan = (int)lround(pan * ONVIF_FOV_PAN_STEPS);
            int requested_tilt = (int)lround(tilt * ONVIF_FOV_TILT_STEPS);
            int target_pan = onvif_clamp_target(status.pan + requested_pan,
                status.pan_min, status.pan_max);
            int target_tilt = onvif_clamp_target(status.tilt + requested_tilt,
                status.tilt_min, status.tilt_max);
            int actual_pan = target_pan - status.pan;
            int actual_tilt = target_tilt - status.tilt;

            fprintf(stderr,
                "ONVIF RelativeMove norm=(%.6f,%.6f) state=(busy=%d calibrated=%d pan=%d[%d,%d] tilt=%d[%d,%d]) target=(%d,%d) delta=(%d,%d)\n",
                pan, tilt, status.busy, status.calibrated, status.pan,
                status.pan_min, status.pan_max, status.tilt, status.tilt_min,
                status.tilt_max, target_pan, target_tilt, actual_pan,
                actual_tilt);
            if (status.busy) rc = -EBUSY;
            else if (!actual_pan && !actual_tilt) rc = 0;
            else rc = ptz_move_relative(actual_pan, actual_tilt);
            if (rc)
                fprintf(stderr, "ONVIF RelativeMove backend rc=%d\n", rc);
        }
    } else if (EQUALS(action, "ContinuousMove")) {
        double pan, tilt;
        if (onvif_xml_attribute(payload, "PanTilt", "x", &pan) ||
            onvif_xml_attribute(payload, "PanTilt", "y", &tilt)) rc = -EINVAL;
        else rc = ptz_move_relative((int)lround(pan * 64.0),
            (int)lround(tilt * 24.0));
    } else if (EQUALS(action, "Stop")) rc = 0;
    else return 0;
    if (rc) onvif_ptz_fault(response, respLen, rc);
    else onvif_ptz_empty(response, respLen, action);
    return 1;
}
