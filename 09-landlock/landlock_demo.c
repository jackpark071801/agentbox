#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int landlock_create_ruleset(
    const struct landlock_ruleset_attr *attr,
    size_t size,
    __u32 flags)
{
    return syscall(SYS_landlock_create_ruleset, attr, size, flags);
}

static int landlock_add_rule(
    int ruleset_fd,
    enum landlock_rule_type rule_type,
    const void *rule_attr,
    __u32 flags)
{
    return syscall(
        SYS_landlock_add_rule,
        ruleset_fd,
        rule_type,
        rule_attr,
        flags
    );
}

static int landlock_restrict_self(int ruleset_fd, __u32 flags)
{
    return syscall(SYS_landlock_restrict_self, ruleset_fd, flags);
}

static void try_read(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        printf("READ %-30s FAILED: %s\n", path, strerror(errno));
        return;
    }

    char buffer[128];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);

    if (n < 0) {
        printf("READ %-30s FAILED: %s\n", path, strerror(errno));
        close(fd);
        return;
    }

    buffer[n] = '\0';

    printf("READ %-30s SUCCESS: %s", path, buffer);

    close(fd);
}

static void try_write(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        printf("WRITE %-29s FAILED: %s\n", path, strerror(errno));
        return;
    }

    const char *message = "MODIFIED BY SANDBOX\n";
    write(fd, message, strlen(message));

    printf("WRITE %-29s SUCCESS\n", path);

    close(fd);
}

static void try_create(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd < 0) {
        printf("CREATE %-26s FAILED: %s\n", path, strerror(errno));
        return;
    }

    printf("CREATE %-26s SUCCESS\n", path);
    close(fd);
}

static void try_remove(const char *path)
{
    if (unlink(path) < 0) {
        printf("REMOVE %-26s FAILED: %s\n", path, strerror(errno));
        return;
    }

    printf("REMOVE %-26s SUCCESS\n", path);
}

int main(void)
{
    printf("PID: %d\n", getpid());

    int abi = landlock_create_ruleset(
        NULL,
        0,
        LANDLOCK_CREATE_RULESET_VERSION
    );

    if (abi < 0) {
        perror("landlock_create_ruleset");
        return 1;
    }

    printf("Landlock ABI version: %d\n", abi);

    struct landlock_ruleset_attr ruleset = {
        .handled_access_fs =
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_DIR |
            LANDLOCK_ACCESS_FS_MAKE_REG |
            LANDLOCK_ACCESS_FS_MAKE_DIR |
            LANDLOCK_ACCESS_FS_TRUNCATE
    };

    int ruleset_fd = landlock_create_ruleset(
        &ruleset,
        sizeof(ruleset),
        0
    );

    if (ruleset_fd < 0) {
        perror("landlock_create_ruleset");
        return 1;
    }

    /*
     * Allow read access to the "allowed" directory.
     */
    int allowed_fd = open("allowed", O_PATH | O_DIRECTORY);

    if (allowed_fd < 0) {
        perror("open allowed");
        close(ruleset_fd);
        return 1;
    }

    struct landlock_path_beneath_attr allowed_rule = {
        .parent_fd = allowed_fd,
        .allowed_access =
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_DIR |
            LANDLOCK_ACCESS_FS_MAKE_REG |
            LANDLOCK_ACCESS_FS_MAKE_DIR |
            LANDLOCK_ACCESS_FS_TRUNCATE
    };

    if (landlock_add_rule(
            ruleset_fd,
            LANDLOCK_RULE_PATH_BENEATH,
            &allowed_rule,
            0) < 0) {
        perror("landlock_add_rule");
        close(allowed_fd);
        close(ruleset_fd);
        return 1;
    }

    close(allowed_fd);

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("prctl(PR_SET_NO_NEW_PRIVS)");
        close(ruleset_fd);
        return 1;
    }

    if (landlock_restrict_self(ruleset_fd, 0) < 0) {
        perror("landlock_restrict_self");
        close(ruleset_fd);
        return 1;
    }

    close(ruleset_fd);

    printf("\nLandlock policy enforced.\n\n");

    try_read("allowed/public.txt");
    try_read("denied/secret.txt");
    try_read("allowed/../denied/secret.txt");

    try_write("allowed/output.txt");
    try_write("denied/output.txt");

    try_create("allowed/new.txt");
    try_create("denied/new.txt");

    try_remove("allowed/output.txt");
    try_remove("denied/output.txt");

    return 0;
}
