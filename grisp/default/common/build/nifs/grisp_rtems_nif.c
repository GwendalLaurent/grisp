#define STATIC_ERLANG_NIF 1
#include <erl_nif.h>

#include <errno.h>
#include <fcntl.h>
#include <rtems/libio.h>
#include <rtems/rtems/clock.h>
#include <rtems/shell.h>
#include <string.h>
#include <unistd.h>

#define MAX_SHELL_ARGS 128
#define BUFFER_SIZE 1024

static ERL_NIF_TERM clock_get_ticks_per_second(ErlNifEnv *env, int argc,
                                               const ERL_NIF_TERM argv[]) {
  return enif_make_long(env, rtems_clock_get_ticks_per_second());
}

static ERL_NIF_TERM clock_get_ticks_since_boot(ErlNifEnv *env, int argc,
                                               const ERL_NIF_TERM argv[]) {
  return enif_make_long(env, rtems_clock_get_ticks_since_boot());
}

static ERL_NIF_TERM clock_get_tod(ErlNifEnv *env, int argc,
                                  const ERL_NIF_TERM argv[]) {
  rtems_time_of_day now;
  rtems_status_code sc = rtems_clock_get_tod(&now);

  if (sc == RTEMS_SUCCESSFUL) {
    return enif_make_tuple7(
        env, enif_make_uint(env, now.year), enif_make_uint(env, now.month),
        enif_make_uint(env, now.day), enif_make_uint(env, now.hour),
        enif_make_uint(env, now.minute), enif_make_uint(env, now.second),
        enif_make_uint(env, now.ticks));
  } else if (sc == RTEMS_NOT_DEFINED) {
    return enif_raise_exception(env, enif_make_atom(env, "RTEMS_NOT_DEFINED"));
  } else if (sc == RTEMS_INVALID_ADDRESS) {
    return enif_raise_exception(env,
                                enif_make_atom(env, "RTEMS_INVALID_ADDRESS"));
  } else {
    return enif_make_badarg(env);
  }
}

// struct rtems_tod_control {
//     uint32_t year;   /* greater than 1987 */
//     uint32_t month;  /* 1 - 12 */
//     uint32_t day;    /* 1 - 31 */
//     uint32_t hour;   /* 0 - 23 */
//     uint32_t minute; /* 0 - 59 */
//     uint32_t second; /* 0 - 59 */
//     uint32_t ticks;  /* elapsed between seconds */
// };
static ERL_NIF_TERM clock_set(ErlNifEnv *env, int argc,
                              const ERL_NIF_TERM argv[]) {
  int arity = -1;
  const ERL_NIF_TERM *ptr;
  rtems_time_of_day time;
  rtems_status_code sc;

  if (argc != 1 || !enif_get_tuple(env, argv[0], &arity, &ptr)) {
    return enif_make_badarg(env);
  }

  if (arity != 7) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_tuple_len"));
  }

  if (!enif_get_uint(env, ptr[0], &time.year)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_year"));
  }
  if (!enif_get_uint(env, ptr[1], &time.month)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_month"));
  }
  if (!enif_get_uint(env, ptr[2], &time.day)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_day"));
  }
  if (!enif_get_uint(env, ptr[3], &time.hour)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_hour"));
  }
  if (!enif_get_uint(env, ptr[4], &time.minute)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_minute"));
  }
  if (!enif_get_uint(env, ptr[5], &time.second)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_second"));
  }
  if (!enif_get_uint(env, ptr[6], &time.ticks)) {
    return enif_raise_exception(env, enif_make_atom(env, "invalid_ticks"));
  }

  sc = rtems_clock_set(&time);

  if (sc == RTEMS_SUCCESSFUL) {
    return enif_make_atom(env, "RTEMS_SUCCESSFUL");
  } else if (sc == RTEMS_INVALID_ADDRESS) {
    return enif_raise_exception(env,
                                enif_make_atom(env, "RTEMS_INVALID_ADDRESS"));
  } else if (sc == RTEMS_INVALID_CLOCK) {
    return enif_raise_exception(env,
                                enif_make_atom(env, "RTEMS_INVALID_CLOCK"));
  } else {
    return enif_make_badarg(env);
  }
}

static ERL_NIF_TERM unmount_nif(ErlNifEnv *env, int argc,
                                const ERL_NIF_TERM argv[]) {
  int status;
  ErlNifBinary path;

  if (!enif_inspect_iolist_as_binary(env, argv[0], &path))
    return enif_make_badarg(env);

  status = unmount((char *)path.data);

  if (status < 0)
    return enif_make_tuple2(
        env, enif_make_atom(env, "error"),
        enif_make_string(env, strerror(errno), ERL_NIF_LATIN1));

  return enif_make_atom(env, "ok");
}

static ERL_NIF_TERM pwrite_nif(ErlNifEnv *env, int argc,
                               const ERL_NIF_TERM argv[]) {
  int fd;
  int offset;
  int bytes_written;
  int close_status;
  int err;
  ErlNifBinary device_path, buffer;

  if (!enif_inspect_iolist_as_binary(env, argv[0], &device_path))
    return enif_make_badarg(env);

  if (!enif_inspect_iolist_as_binary(env, argv[1], &buffer))
    return enif_make_badarg(env);

  if (!enif_get_int(env, argv[2], &offset))
    return enif_make_badarg(env);

  fd = open((const char *)device_path.data, O_RDWR);
  if (fd < 0) {
    err = errno;
    return enif_make_tuple3(
        env, enif_make_atom(env, "error"), enif_make_atom(env, "open"),
        enif_make_string(env, strerror(err), ERL_NIF_LATIN1));
  }

  bytes_written = pwrite(fd, buffer.data, buffer.size, offset);
  if (bytes_written < 0) {
    err = errno;
    return enif_make_tuple3(
        env, enif_make_atom(env, "error"), enif_make_atom(env, "pwrite"),
        enif_make_string(env, strerror(err), ERL_NIF_LATIN1));
  }

  close_status = close(fd);
  if (close_status < 0) {
    err = errno;
    return enif_make_tuple3(
        env, enif_make_atom(env, "error"), enif_make_atom(env, "close"),
        enif_make_string(env, strerror(err), ERL_NIF_LATIN1));
  }

  return enif_make_tuple2(env, enif_make_atom(env, "ok"),
                          enif_make_int(env, bytes_written));
}

// static ERL_NIF_TERM nif_rtems_shell_make_args(ErlNifEnv* env, int argc,
//                                               const ERL_NIF_TERM argv[]) {
//     ErlNifBinary command_bin;
//     char command_line[256];
//     char *cmd_argv[MAX_SHELL_ARGS];
//     int cmd_argc;
//     ERL_NIF_TERM list, bin_term;

//     if (argc != 1 || !enif_is_binary(env, argv[0])) {
//         return enif_make_badarg(env);
//     }

//     if (!enif_inspect_binary(env, argv[0], &command_bin)) {
//         return enif_make_badarg(env);
//     }

//     if (command_bin.size >= (sizeof(command_line) - 1)) {
//         return enif_make_badarg(env);
//     }

//     memcpy(command_line, command_bin.data, command_bin.size);
//     command_line[command_bin.size] = '\0';

//     if (rtems_shell_make_args(command_line, &cmd_argc, cmd_argv, MAX_SHELL_ARGS) != 0) {
//         return enif_make_badarg(env);
//     }

//     list = enif_make_list(env, 0);
//     for (int i = cmd_argc - 1; i >= 0; --i) {
//         bin_term = enif_make_binary(env, &command_bin);
//         if (!enif_alloc_binary(strlen(cmd_argv[i]), &command_bin)) {
//             return enif_make_badarg(env);
//         }
//         memcpy(command_bin.data, cmd_argv[i], strlen(cmd_argv[i]));
//         list = enif_make_list_cell(env, bin_term, list);
//         free(cmd_argv[i]);
//     }

//     return enif_make_tuple2(env, enif_make_atom(env, "ok"), list);
// }

static ERL_NIF_TERM raise_rtems_error(ErlNifEnv* env) {
    return enif_raise_exception(env, enif_make_atom(env, "rtems_error"));
}

// static ssize_t read_from_fd(int fd, char *buffer, size_t max_size) {
//   ssize_t total_size = 0;
//   ssize_t read_size;

//   while ((read_size = read(fd, buffer + total_size, max_size - total_size)) > 0) {
//     total_size += read_size;
//     if (total_size >= max_size)
//       break;
//   }

//   if (read_size < 0)
//     return -1;
//   else if (read_size == 0)
//     return total_size;

//   return total_size;
// }

static ERL_NIF_TERM nif_rtems_shell_execute_cmd(ErlNifEnv* env, int argc,
                                                const ERL_NIF_TERM argv[]) {
    // FILE *old_stdout = stdout, *old_stderr = stderr;
    ErlNifBinary command_bin;
    // ErlNifBinary stdout_bin, stderr_bin;
    char command_line[256];
    char *cmd_argv[MAX_SHELL_ARGS];
    int status_code, i, cmd_argc = 0;
    ERL_NIF_TERM list, head, tail, result;
    // char stdout_buffer[BUFFER_SIZE];
    // char stderr_buffer[BUFFER_SIZE];
    // ssize_t stdout_size, stderr_size;
    // int stdout_pipe[2];
    // int stderr_pipe[2];
    bool env_setup = false;
    rtems_shell_env_t shell_env = {
        .magic         = rtems_build_name('S', 'E', 'N', 'V'),
        .managed       = false,
        .devname       = NULL,
        .taskname      = NULL,
        .exit_shell    = false,
        .forever       = true,
        .echo          = false,
        .cwd           = "/",
        .input         = NULL,
        .output        = NULL,
        .output_append = false,
        .parent_stdin  = NULL,
        .parent_stdout = NULL,
        .parent_stderr = NULL,
        .wake_on_end   = RTEMS_ID_NONE,
        .exit_code     = NULL,
        .login_check   = NULL,
        .uid           = 0,
        .gid           = 0
    };

    if (argc < 2) {
        return enif_make_badarg(env);
    }

    if (!enif_is_binary(env, argv[0]) || !enif_is_list(env, argv[1])) {
        return enif_make_badarg(env);
    }

    if (!enif_inspect_binary(env, argv[0], &command_bin)) {
        return enif_make_badarg(env);
    }

    if (command_bin.size >= (sizeof(command_line) - 1)) {
        return enif_make_badarg(env);
    }

    memcpy(command_line, command_bin.data, command_bin.size);
    command_line[command_bin.size] = '\0';

    list = argv[1];

    while (enif_get_list_cell(env, list, &head, &tail)) {
        ErlNifBinary arg_bin;
        if (!enif_inspect_binary(env, head, &arg_bin)) {
            for (i = 0; i < cmd_argc; ++i) {
                enif_free(cmd_argv[i]);
            }
            return enif_make_badarg(env);
        }

        cmd_argv[cmd_argc] = (char *)enif_alloc(arg_bin.size + 1);
        if (cmd_argv[cmd_argc] == NULL) {
            for (i = 0; i < cmd_argc; ++i) {
                enif_free(cmd_argv[i]);
            }
            return enif_make_badarg(env);
        }
        memcpy(cmd_argv[cmd_argc], arg_bin.data, arg_bin.size);
        cmd_argv[cmd_argc][arg_bin.size] = '\0';
        cmd_argc++;

        list = tail;
    }

    // pipe(stdout_pipe);
    // pipe(stderr_pipe);

    // stdout = stdout_pipe[1];
    // stdin = stderr_pipe[1];

    rtems_shell_init_environment();
    if (!rtems_shell_set_shell_env(shell_env)) {
        result = raise_rtems_error(env);
        goto cleanup;
    }
    env_setup = true;

    if (RTEMS_SUCCESSFUL != rtems_libio_set_private_env()) {
        result = raise_rtems_error(env);
        goto cleanup;
    }

    setuid(shell_env.uid);
    seteuid(shell_env.uid);
    setgid(shell_env.gid);
    setegid(shell_env.gid);
    rtems_current_user_env_getgroups();
    chroot(shell_env.cwd);
    chdir(shell_env.cwd);

    status_code = rtems_shell_execute_cmd(command_line, cmd_argc, cmd_argv);

    fflush(stdout);
    fflush(stderr);

    // close(stdout_pipe[1]);
    // stdout_pipe[1] = 0;
    // close(stderr_pipe[1]);
    // stderr_pipe[1] = 0;

    // stdout_size = read_from_fd(stdout_pipe[0], stdout_buffer, BUFFER_SIZE);
    // stderr_size = read_from_fd(stderr_pipe[0], stderr_buffer, BUFFER_SIZE);

    // close(stdout_pipe[0]);
    // stdout_pipe[0] = 0;
    // close(stderr_pipe[0]);
    // stderr_pipe[0] = 0;

    // enif_alloc_binary(stdout_size, &stdout_bin);
    // enif_alloc_binary(stderr_size, &stderr_bin);
    // memcpy(stdout_bin.data, stdout_buffer, stdout_bin.size);
    // memcpy(stderr_bin.data, stderr_buffer, stderr_bin.size);

    // result = enif_make_tuple3(env,
    //                           enif_make_int(env, status_code),
    //                           enif_make_binary(env, &stdout_bin),
    //                           enif_make_binary(env, &stderr_bin));

    result = enif_make_int(env, status_code);

// cleanup:
    // if (stdout_pipe[0] != 0)
    //     close(stdout_pipe[0]);
    // if (stdout_pipe[1] != 0)
    //     close(stdout_pipe[1]);
    // if (stderr_pipe[0] != 0)
    //     close(stderr_pipe[0]);
    // if (stderr_pipe[1] != 0)
    //     close(stderr_pipe[1]);

    for (i = 0; i < cmd_argc; ++i)
        if (cmd_argv[i] != NULL)
            enif_free(cmd_argv[i]);

    if (env_setup)
        rtems_shell_clear_shell_env();

    // stdout = old_stdout;
    // stderr = old_stderr;

    return result;
}

static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info) {
    return 0;
}

static ErlNifFunc nif_funcs[] = {
    {"clock_get_ticks_per_second", 0, clock_get_ticks_per_second},
    {"clock_get_ticks_since_boot", 0, clock_get_ticks_since_boot},
    {"clock_get_tod_nif", 0, clock_get_tod},
    {"clock_set_nif", 1, clock_set},
    {"unmount_nif", 1, unmount_nif},
    {"pwrite_nif", 3, pwrite_nif},
    // {"shell_make_args_nif", 1, nif_rtems_shell_make_args},
    {"shell_execute_cmd_nif", 2, nif_rtems_shell_execute_cmd/*, ERL_NIF_DIRTY_JOB_CPU_BOUND*/}
};

ERL_NIF_INIT(grisp_rtems, nif_funcs, load, NULL, NULL, NULL)
