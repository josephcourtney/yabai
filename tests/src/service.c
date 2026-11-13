// Pure service-policy tests and temporary-plist parsing tests. Never touch
// ~/Library/LaunchAgents, launchctl state or a running yabai daemon.

TEST_FUNC(service_install_policy_is_idempotent_and_refuses_unsafe_replacement,
{
    TEST_CHECK(service_install_decide(SERVICE_PLIST_MISSING, false, false, false),
               SERVICE_INSTALL_WRITE);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_VALID, true, false, true),
               SERVICE_INSTALL_NOOP);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_VALID, true, true, true),
               SERVICE_INSTALL_NOOP);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_VALID, false, false, false),
               SERVICE_INSTALL_REJECT_MISMATCH);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_VALID, false, true, true),
               SERVICE_INSTALL_REJECT_LOADED);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_VALID, false, true, false),
               SERVICE_INSTALL_WRITE);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_MISSING, false, false, true),
               SERVICE_INSTALL_REJECT_LOADED);
    TEST_CHECK(service_install_decide(SERVICE_PLIST_INVALID, false, true, false),
               SERVICE_INSTALL_REJECT_INVALID);
});

TEST_FUNC(service_xml_escaping_is_lossless,
{
    char *escaped = service_xml_escape("a&<>\"'b");
    TEST_CHECK(strcmp(escaped, "a&amp;&lt;&gt;&quot;&apos;b") == 0, true);
    free(escaped);
});

TEST_FUNC(service_executable_identity_uses_resolved_paths,
{
    char self[4096] = {0};
    bool exists = service_self_executable(self, sizeof(self));
    TEST_CHECK(exists, true);
    if (exists) {
        TEST_CHECK(service_same_executable(self, self), true);
        TEST_CHECK(service_same_executable(self, "/definitely/not-a-real-yabai"), false);
    }
});

TEST_FUNC(service_plist_rejects_other_labels_and_symlinks,
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:
                      [NSString stringWithFormat:@"yabai-service-test-%d.plist", getpid()]];
    NSString *alias = [path stringByAppendingString:@".link"];
    unlink([path fileSystemRepresentation]);
    unlink([alias fileSystemRepresentation]);
    char executable[4096] = {0};
    TEST_CHECK(service_read_plist_executable(
                   [path fileSystemRepresentation], executable, sizeof(executable)),
               SERVICE_PLIST_MISSING);
    NSDictionary *valid = [NSDictionary dictionaryWithObjectsAndKeys:
                           [NSString stringWithUTF8String:_NAME_YABAI_PLIST], @"Label",
                           [NSArray arrayWithObject:@"/tmp/yabai-test-binary"], @"ProgramArguments",
                           nil];
    TEST_CHECK([valid writeToFile:path atomically:YES], true);

    enum service_plist_state state = service_read_plist_executable(
        [path fileSystemRepresentation], executable, sizeof(executable));
    TEST_CHECK(state, SERVICE_PLIST_VALID);
    TEST_CHECK(strcmp(executable, "/tmp/yabai-test-binary") == 0, true);

    NSDictionary *foreign = [NSDictionary dictionaryWithObjectsAndKeys:
                             @"org.example.unrelated", @"Label",
                             [NSArray arrayWithObject:@"/tmp/yabai-test-binary"], @"ProgramArguments",
                             nil];
    TEST_CHECK([foreign writeToFile:path atomically:YES], true);
    state = service_read_plist_executable(
        [path fileSystemRepresentation], executable, sizeof(executable));
    TEST_CHECK(state, SERVICE_PLIST_INVALID);

    // The target is valid here: only the symlink itself should be rejected.
    TEST_CHECK([valid writeToFile:path atomically:YES], true);
    TEST_CHECK(symlink([path fileSystemRepresentation], [alias fileSystemRepresentation]) == 0, true);
    state = service_read_plist_executable(
        [alias fileSystemRepresentation], executable, sizeof(executable));
    TEST_CHECK(state, SERVICE_PLIST_INVALID);
    unlink([alias fileSystemRepresentation]);
    unlink([path fileSystemRepresentation]);
    [pool drain];
});
