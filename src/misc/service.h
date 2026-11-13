#ifndef SERVICE_H
#define SERVICE_H

#define _PATH_LAUNCHCTL   "/bin/launchctl"
#define _NAME_YABAI_PLIST "com.asmvik.yabai"
#define _PATH_YABAI_PLIST "%s/Library/LaunchAgents/"_NAME_YABAI_PLIST".plist"

#define _YABAI_PLIST \
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
    "<plist version=\"1.0\">\n" \
    "<dict>\n" \
    "    <key>Label</key>\n" \
    "    <string>"_NAME_YABAI_PLIST"</string>\n" \
    "    <key>ProgramArguments</key>\n" \
    "    <array>\n" \
    "        <string>%s</string>\n" \
    "    </array>\n" \
    "    <key>EnvironmentVariables</key>\n" \
    "    <dict>\n" \
    "        <key>PATH</key>\n" \
    "        <string>%s</string>\n" \
    "    </dict>\n" \
    "    <key>RunAtLoad</key>\n" \
    "    <true/>\n" \
    "    <key>KeepAlive</key>\n" \
    "    <dict>\n" \
    "        <key>SuccessfulExit</key>\n" \
    " 	     <false/>\n" \
    " 	     <key>Crashed</key>\n" \
    " 	     <true/>\n" \
    "    </dict>\n" \
    "    <key>StandardOutPath</key>\n" \
    "    <string>/tmp/yabai_%s.out.log</string>\n" \
    "    <key>StandardErrorPath</key>\n" \
    "    <string>/tmp/yabai_%s.err.log</string>\n" \
    "    <key>ProcessType</key>\n" \
    "    <string>Interactive</string>\n" \
    "    <key>Nice</key>\n" \
    "    <integer>-20</integer>\n" \
    "</dict>\n" \
    "</plist>"

//
// NOTE(asmvik): A launchd service has the following states:
//
//          1. Installed / Uninstalled
//          2. Active (Enable / Disable)
//          3. Bootstrapped (Load / Unload)
//          4. Running (Start / Stop)
//

static int safe_exec(char *const argv[], bool suppress_output)
{
    pid_t pid;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (suppress_output) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0);
    }

    int status = posix_spawn(&pid, argv[0], &actions, NULL, argv, NULL);
    if (status) return 1;

    while ((waitpid(pid, &status, 0) == -1) && (errno == EINTR)) {
        usleep(1000);
    }

    if (WIFSIGNALED(status)) {
        return 1;
    } else if (WIFSTOPPED(status)) {
        return 1;
    } else {
        return WEXITSTATUS(status);
    }
}

static char *populate_plist_path(void)
{
    CFStringRef home_ref = (__bridge CFStringRef) NSHomeDirectoryForUser(NULL);
    char *home = home_ref ? cfstring_copy(home_ref) : NULL;

    if (!home) {
        error("yabai: unable to retrieve home directory! abort..\n");
    }

    int size = strlen(_PATH_YABAI_PLIST)-2 + strlen(home) + 1;
    char *result = malloc(size);
    if (!result) {
        error("yabai: could not allocate memory for plist path! abort..\n");
    }

    memset(result, 0, size);
    snprintf(result, size, _PATH_YABAI_PLIST, home);

    return result;
}

// Service identity is the absolute executable selected by the caller, not
// whichever yabai happens to be on PATH (which may be Homebrew's binary).
static bool service_self_executable(char *output, size_t capacity)
{
    char path[4096];
    uint32_t length = sizeof(path);
    if (_NSGetExecutablePath(path, &length) != 0) return false;
    return realpath(path, output) != NULL && strlen(output) < capacity;
}

static bool service_same_executable(const char *left, const char *right)
{
    if (!left || !right || !left[0] || !right[0]) return false;
    char a[4096], b[4096];
    if (realpath(left, a) && realpath(right, b)) {
        return strcmp(a, b) == 0;
    }
    // A removed binary is never silently treated as the current executable.
    return false;
}

enum service_plist_state
{
    SERVICE_PLIST_MISSING,
    SERVICE_PLIST_VALID,
    SERVICE_PLIST_INVALID
};

// Accept only the expected launchd label and an absolute first executable.
// Never overwrite a foreign or malformed plist, even with --replace.
static enum service_plist_state service_read_plist_executable(const char *path,
                                                              char *configured,
                                                              size_t capacity)
{
    if (configured && capacity) configured[0] = '\0';
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? SERVICE_PLIST_MISSING : SERVICE_PLIST_INVALID;
    }
    if (!S_ISREG(st.st_mode)) return SERVICE_PLIST_INVALID;

    NSString *file = [NSString stringWithUTF8String:path];
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:file];
    if (![plist isKindOfClass:[NSDictionary class]]) return SERVICE_PLIST_INVALID;

    id label = [plist objectForKey:@"Label"];
    id arguments = [plist objectForKey:@"ProgramArguments"];
    if (![label isKindOfClass:[NSString class]] ||
        ![label isEqualToString:[NSString stringWithUTF8String:_NAME_YABAI_PLIST]] ||
        ![arguments isKindOfClass:[NSArray class]] ||
        [arguments count] == 0) return SERVICE_PLIST_INVALID;

    id executable = [arguments objectAtIndex:0];
    if (![executable isKindOfClass:[NSString class]]) return SERVICE_PLIST_INVALID;
    const char *value = [executable UTF8String];
    if (!value || value[0] != '/' || strlen(value) >= capacity) return SERVICE_PLIST_INVALID;
    memcpy(configured, value, strlen(value) + 1);
    return SERVICE_PLIST_VALID;
}

// launchctl's successful 'print' means the job is bootstrapped. Its PID is
// present only when there is a running instance. The command is composed
// entirely of constant text and a numeric UID (no user-controlled shell input).
static bool service_launchd_job(const char *label, pid_t *running_pid)
{
    if (running_pid) *running_pid = 0;
    char command[256];
    int length = snprintf(command, sizeof(command),
                          "/bin/launchctl print gui/%u/%s 2>/dev/null",
                          (unsigned)getuid(), label);
    if (length <= 0 || length >= (int)sizeof(command)) return false;
    FILE *pipe = popen(command, "r");
    if (!pipe) return false;

    char line[1024];
    while (fgets(line, sizeof(line), pipe)) {
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (strncmp(cursor, "pid = ", 6) == 0 && running_pid) {
            char *end = NULL;
            errno = 0;
            long value = strtol(cursor + 6, &end, 10);
            if (!errno && end != cursor + 6 && value > 0 && value <= INT_MAX) {
                *running_pid = (pid_t)value;
            }
        }
    }
    int status = pclose(pipe);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool service_message_socket_reachable(void)
{
    const char *user = getenv("USER");
    if (!user) return false;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    int length = snprintf(path, sizeof(path), "/tmp/yabai_%s.socket", user);
    if (length <= 0 || length >= (int)sizeof(path)) return false;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, (size_t)length + 1);
    bool reachable = connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0;
    close(fd);
    return reachable;
}

static int service_status(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    char *path = populate_plist_path();
    char configured[4096] = {0}, invoked[4096] = {0};
    bool have_invoked = service_self_executable(invoked, sizeof(invoked));
    enum service_plist_state state = service_read_plist_executable(
        path, configured, sizeof(configured));
    pid_t pid = 0;
    bool loaded = service_launchd_job(_NAME_YABAI_PLIST, &pid);
    char running[PROC_PIDPATHINFO_MAXSIZE] = {0};
    bool have_running = pid > 0 && proc_pidpath(pid, running, sizeof(running)) > 0;
    bool matching = state == SERVICE_PLIST_VALID && have_invoked &&
                    service_same_executable(configured, invoked);
    bool running_matches = have_running && have_invoked &&
                            service_same_executable(running, invoked);
    bool socket_ready = pid > 0 && service_message_socket_reachable();

    fprintf(stdout, "service: %s\n", _NAME_YABAI_PLIST);
    fprintf(stdout, "plist: %s\n", path);
    fprintf(stdout, "installed: %s\n",
            state == SERVICE_PLIST_MISSING ? "no" :
            state == SERVICE_PLIST_VALID ? "yes" : "invalid");
    fprintf(stdout, "configured-executable: %s\n",
            state == SERVICE_PLIST_VALID ? configured : "(unavailable)");
    fprintf(stdout, "invoked-executable: %s\n",
            have_invoked ? invoked : "(unavailable)");
    fprintf(stdout, "executable-match: %s\n", matching ? "yes" : "no");
    fprintf(stdout, "bootstrapped: %s\n", loaded ? "yes" : "no");
    fprintf(stdout, "pid: %d\n", (int)pid);
    fprintf(stdout, "running-executable: %s\n",
            have_running ? running : "(unavailable)");
    fprintf(stdout, "running-executable-match: %s\n",
            running_matches ? "yes" : "no");
    fprintf(stdout, "message-socket: %s\n", socket_ready ? "reachable" : "unreachable");

    const char *other_labels[] = {"com.koekeishiya.yabai", "homebrew.mxcl.yabai"};
    bool conflict = false;
    for (int i = 0; i < (int)(sizeof(other_labels)/sizeof(other_labels[0])); ++i) {
        if (service_launchd_job(other_labels[i], NULL)) {
            fprintf(stdout, "competing-service: %s\n", other_labels[i]);
            conflict = true;
        }
    }
    int result = state == SERVICE_PLIST_VALID && matching && loaded &&
                 pid > 0 && running_matches && socket_ready && !conflict ? 0 : 1;
    free(path);
    [pool drain];
    return result;
}

// Escape substituted strings so PATH and executable paths containing XML
// metacharacters cannot produce an invalid or misleading launchd plist.
static char *service_xml_escape(const char *value)
{
    size_t length = strlen(value);
    size_t extra = 0;
    for (const char *p = value; *p; ++p) {
        switch (*p) {
        case '&': extra += 4; break;
        case '<': case '>': extra += 3; break;
        case '"': case '\'': extra += 5; break;
        }
    }
    if (length > SIZE_MAX - extra - 1) error("yabai: XML attribute too long\n");
    char *output = malloc(length + extra + 1);
    if (!output) error("yabai: XML escape allocation failed\n");
    char *cursor = output;
    for (const char *p = value; *p; ++p) {
        const char *encoded = NULL;
        switch (*p) {
        case '&': encoded = "&amp;"; break;
        case '<': encoded = "&lt;"; break;
        case '>': encoded = "&gt;"; break;
        case '"': encoded = "&quot;"; break;
        case '\'': encoded = "&apos;"; break;
        }
        if (encoded) {
            size_t count = strlen(encoded);
            memcpy(cursor, encoded, count);
            cursor += count;
        } else {
            *cursor++ = *p;
        }
    }
    *cursor = '\0';
    return output;
}

static char *populate_plist(int *length)
{
    char *user = getenv("USER");
    if (!user) {
        error("yabai: 'env USER' not set! abort..\n");
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        error("yabai: 'env PATH' not set! abort..\n");
    }

    char exe_path[4096];
    unsigned int exe_path_size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &exe_path_size) < 0) {
        error("yabai: unable to retrieve path of executable! abort..\n");
    }

    char *escaped_exe = service_xml_escape(exe_path);
    char *escaped_path = service_xml_escape(path_env);
    char *escaped_user = service_xml_escape(user);
    size_t size = strlen(_YABAI_PLIST)-8 + strlen(escaped_exe) +
                  strlen(escaped_path) + 2*strlen(escaped_user) + 1;
    if (size > INT_MAX) error("yabai: service plist is too large\n");
    char *result = malloc(size);
    if (!result) error("yabai: could not allocate memory for plist contents!\n");

    int written = snprintf(result, size, _YABAI_PLIST,
                           escaped_exe, escaped_path, escaped_user, escaped_user);
    free(escaped_exe);
    free(escaped_path);
    free(escaped_user);
    if (written < 0 || (size_t)written >= size) {
        free(result);
        error("yabai: service plist formatting failed\n");
    }
    *length = written;
    return result;
}

static inline void ensure_directory_exists(char *yabai_plist_path)
{
    //
    // NOTE(asmvik): Temporarily remove filename.
    // We know the filepath will contain a slash, as
    // it is controlled by us, so don't bother checking
    // the result..
    //

    char *last_slash = strrchr(yabai_plist_path, '/');
    *last_slash = '\0';

    if (!directory_exists(yabai_plist_path)) {
        mkdir(yabai_plist_path, 0755);
    }

    //
    // NOTE(asmvik): Restore original filename.
    //

    *last_slash = '/';
}

static int service_install_internal(char *yabai_plist_path)
{
    int length = 0;
    char *contents = populate_plist(&length);
    ensure_directory_exists(yabai_plist_path);

    // Never truncate an existing plist in place. Write a complete private
    // temporary file on the same filesystem, then atomically rename it.
    size_t size = strlen(yabai_plist_path) + sizeof(".tmp.XXXXXX");
    char *temporary = malloc(size);
    if (!temporary) { free(contents); return 1; }
    snprintf(temporary, size, "%s.tmp.XXXXXX", yabai_plist_path);

    int fd = mkstemp(temporary);
    if (fd < 0) { free(temporary); free(contents); return 1; }
    int result = 0;
    if (fchmod(fd, 0644) != 0) result = 1;
    size_t written = 0;
    while (!result && written < (size_t)length) {
        ssize_t count = write(fd, contents + written, (size_t)length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { result = 1; break; }
        written += (size_t)count;
    }
    if (!result && fsync(fd) != 0) result = 1;
    if (close(fd) != 0) result = 1;
    if (!result && rename(temporary, yabai_plist_path) != 0) result = 1;
    if (result) unlink(temporary);
    free(temporary);
    free(contents);
    return result;
}

static int service_install(bool replace)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    char *path = populate_plist_path();
    char configured[4096] = {0}, current[4096] = {0};
    enum service_plist_state state = service_read_plist_executable(
        path, configured, sizeof(configured));
    if (!service_self_executable(current, sizeof(current))) {
        warn("yabai: cannot resolve this executable; refusing service installation\n");
        free(path); [pool drain]; return 1;
    }

    if (state == SERVICE_PLIST_INVALID) {
        warn("yabai: '%s' is malformed, a symlink, or uses another label; refusing to overwrite\n", path);
        free(path); [pool drain]; return 1;
    }

    if (state == SERVICE_PLIST_VALID &&
        service_same_executable(configured, current)) {
        printf("yabai: service already installed for %s\n", current);
        free(path); [pool drain]; return 0;
    }

    if (state == SERVICE_PLIST_VALID && !replace) {
        warn("yabai: service executable mismatch\n  installed: %s\n  invoked:   %s\n"
             "Use --install-service --replace to update it explicitly\n",
             configured, current);
        free(path); [pool drain]; return 1;
    }

    if (service_launchd_job(_NAME_YABAI_PLIST, NULL)) {
        warn("yabai: service is loaded; run --stop-service before installation/replacement\n");
        free(path); [pool drain]; return 1;
    }

    int result = service_install_internal(path);
    if (!result) {
        printf("yabai: %s service for %s\n",
               state == SERVICE_PLIST_MISSING ? "installed" : "replaced", current);
    } else {
        warn("yabai: failed to write service plist '%s'\n", path);
    }
    free(path);
    [pool drain];
    return result;
}

static int service_uninstall(void)
{
    char *yabai_plist_path = populate_plist_path();

    if (!file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is not installed! abort..\n", yabai_plist_path);
    }

    return unlink(yabai_plist_path) == 0 ? 0 : 1;
}

static int service_start(void)
{
    // Keep the historical auto-install behavior, but never start a plist
    // pointing at a different yabai binary without explicit replacement.
    if (service_install(false) != 0) return 1;
    char *yabai_plist_path = populate_plist_path();

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped and could be disabled.
        // There is no way to query if the service is disabled, and we cannot
        // bootstrap a disabled service. Try to enable the service. This will be
        // a no-op if the service is already enabled.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "enable", service_target, NULL };
        safe_exec((char *const*)args, false);

        //
        // NOTE(asmvik): Bootstrap service into the target domain.
        // This will also start the program **iff* RunAtLoad is set to true.
        //

        const char *const args2[] = { _PATH_LAUNCHCTL, "bootstrap", domain_target, yabai_plist_path, NULL };
        return safe_exec((char *const*)args2, false);
    } else {

        //
        // NOTE(asmvik): The service has already been bootstrapped.
        // Tell the bootstrapped service to launch immediately; it is an
        // error to bootstrap a service that has already been bootstrapped.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", service_target, NULL };
        return safe_exec((char *const*)args, false);
    }
}

static int service_restart(void)
{
    char *yabai_plist_path = populate_plist_path();
    char configured[4096] = {0}, invoked[4096] = {0};
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    bool valid = service_read_plist_executable(
        yabai_plist_path, configured, sizeof(configured)) == SERVICE_PLIST_VALID &&
        service_self_executable(invoked, sizeof(invoked)) &&
        service_same_executable(configured, invoked);
    [pool drain];
    if (!valid) {
        warn("yabai: service is missing, invalid, or configured for another binary; "
             "check --service-status\n");
        free(yabai_plist_path);
        return 1;
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", "-k", service_target, NULL };
    return safe_exec((char *const*)args, false);
}

static int service_stop(void)
{
    char *yabai_plist_path = populate_plist_path();
    if (!file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is not installed! abort..\n", yabai_plist_path);
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped, but the program
        // could still be running an instance that was started **while the service
        // was bootstrapped**, so we tell it to stop said service.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kill", "SIGTERM", service_target, NULL };
        return safe_exec((char *const*)args, false);
    } else {

        //
        // NOTE(asmvik): Service is bootstrapped; we stop a potentially
        // running instance of the program and unload the service, making it
        // not trigger automatically in the future.
        //
        // This is NOT the same as disabling the service, which will prevent
        // it from being boostrapped in the future (without explicitly re-enabling
        // it first).
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "bootout", domain_target, yabai_plist_path, NULL };
        safe_exec((char *const*)args, false);

        const char *const args2[] = { _PATH_LAUNCHCTL, "disable", service_target, NULL };
        return safe_exec((char *const*)args2, false);
    }
}

#endif
