#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "runtime/env.h"
#include "runtime/runtime.h"
#include "runtime/supervisor.h"

#ifndef PHP_BIN
#define PHP_BIN "/usr/bin/php8.3"
#endif

static volatile sig_atomic_t child_pid = -1;
static volatile sig_atomic_t forwarded_signal = 0;

static void print_help(void)
{
    printf("Usage: start [APP_ARGUMENTS...]\n\n");
    printf("Start a packed YAPF application from the current app folder.\n\n");
    printf("Required files in the same folder:\n");
    printf("  start\n");
    printf("  yapf_loader.so\n");
    printf("  app.yapfc\n");
    printf("  license.yapfl\n");
    printf("  license.yapfs\n");
    printf("  .env\n");
    printf("  storage/\n\n");
    printf("Environment:\n");
    printf("  YAPF_PHP_BIN overrides the PHP binary. Default: %s\n", PHP_BIN);
    printf("  YAPF_LICENSE_CHECK_INTERVAL overrides the runtime license check interval. Default: 60\n");
}

static void forward_signal(int signum)
{
    forwarded_signal = signum;
    if (child_pid > 0) {
        kill((pid_t)child_pid, signum);
    }
}

static unsigned int license_check_interval(void)
{
    const char *value = getenv("YAPF_LICENSE_CHECK_INTERVAL");
    char *end = NULL;
    unsigned long parsed;

    if (!value || !*value) {
        return 60;
    }

    parsed = strtoul(value, &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > 86400UL) {
        return 60;
    }

    return (unsigned int)parsed;
}

static int wait_after_terminate(pid_t pid)
{
    int status = 0;

    kill(pid, SIGTERM);
    for (int i = 0; i < 5; i++) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return status;
        }
        sleep(1);
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return status;
}

static int exit_status_from_wait(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static int supervise_php(pid_t pid, const char *bundle, const char *license, const char *state)
{
    unsigned int interval = license_check_interval();
    time_t next_check = time(NULL) + (time_t)interval;
    int status = 0;
    char error[256];

    child_pid = pid;
    signal(SIGTERM, forward_signal);
    signal(SIGINT, forward_signal);
    signal(SIGHUP, forward_signal);

    while (1) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            child_pid = -1;
            return exit_status_from_wait(status);
        }
        if (result < 0 && errno != EINTR) {
            child_pid = -1;
            fprintf(stderr, "cannot wait for PHP process: %s\n", strerror(errno));
            return 1;
        }

        if (forwarded_signal) {
            result = waitpid(pid, &status, 0);
            child_pid = -1;
            if (result == pid) {
                return exit_status_from_wait(status);
            }
            return 128 + forwarded_signal;
        }

        if (time(NULL) >= next_check) {
            if (yapf_supervisor_check(bundle, license, state, error, sizeof(error)) != 0) {
                fprintf(stderr, "%s\n", error);
                status = wait_after_terminate(pid);
                child_pid = -1;
                return exit_status_from_wait(status) ? exit_status_from_wait(status) : 1;
            }
            next_check = time(NULL) + (time_t)interval;
        }

        sleep(1);
    }
}

int main(int argc, char **argv)
{
    char dir[PATH_MAX];
    char loader[PATH_MAX];
    char bundle[PATH_MAX];
    char license[PATH_MAX];
    char state[PATH_MAX];
    char env[PATH_MAX];
    char d_loader[PATH_MAX + 32];
    char d_bundle[PATH_MAX + 32];
    char d_license[PATH_MAX + 32];
    char d_state[PATH_MAX + 32];
    char d_env[PATH_MAX + 32];
    const char *php = getenv("YAPF_PHP_BIN");
    char **php_args;
    int arg_pos = 0;
    pid_t pid;

    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        return 0;
    }

    if (!php || !*php) {
        php = PHP_BIN;
    }

    yapf_app_dir(dir, sizeof(dir));
    yapf_join_path(loader, sizeof(loader), dir, "yapf_loader.so");
    yapf_join_path(bundle, sizeof(bundle), dir, "app.yapfc");
    yapf_join_path(license, sizeof(license), dir, "license.yapfl");
    yapf_join_path(state, sizeof(state), dir, "license.yapfs");
    yapf_join_path(env, sizeof(env), dir, ".env");

    if (!yapf_path_exists(loader) || !yapf_path_exists(bundle) || !yapf_path_exists(license) || !yapf_path_exists(state)) {
        fprintf(stderr, "missing runtime files in %s\n", dir);
        return 1;
    }

    if (chdir(dir) != 0) {
        fprintf(stderr, "cannot enter application directory: %s\n", strerror(errno));
        return 1;
    }

    yapf_env_load_file(env);

    snprintf(d_loader, sizeof(d_loader), "extension=%s", loader);
    snprintf(d_bundle, sizeof(d_bundle), "yapf.bundle=%s", bundle);
    snprintf(d_license, sizeof(d_license), "yapf.license=%s", license);
    snprintf(d_state, sizeof(d_state), "yapf.state=%s", state);
    snprintf(d_env, sizeof(d_env), "yapf.env=%s", env);

    php_args = calloc((size_t)argc + 16, sizeof(char *));
    if (!php_args) {
        fprintf(stderr, "cannot allocate PHP arguments\n");
        return 1;
    }

    php_args[arg_pos++] = (char *)php;
    php_args[arg_pos++] = "-d";
    php_args[arg_pos++] = d_loader;
    php_args[arg_pos++] = "-d";
    php_args[arg_pos++] = d_bundle;
    php_args[arg_pos++] = "-d";
    php_args[arg_pos++] = d_license;
    php_args[arg_pos++] = "-d";
    php_args[arg_pos++] = d_state;
    php_args[arg_pos++] = "-d";
    php_args[arg_pos++] = d_env;
    php_args[arg_pos++] = "-r";
    php_args[arg_pos++] = "if (function_exists('yapf_start')) { yapf_start(); } else { fwrite(STDERR, \"YAPF loader is not active\\n\"); exit(1); }";
    php_args[arg_pos++] = "--";
    for (int i = 1; i < argc; i++) {
        php_args[arg_pos++] = argv[i];
    }
    php_args[arg_pos] = NULL;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "cannot fork PHP process: %s\n", strerror(errno));
        free(php_args);
        return 1;
    }

    if (pid == 0) {
        execvp(php, php_args);
        fprintf(stderr, "cannot start PHP: %s\n", strerror(errno));
        _exit(127);
    }

    free(php_args);
    return supervise_php(pid, bundle, license, state);
}
