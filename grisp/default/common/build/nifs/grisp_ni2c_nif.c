#define STATIC_ERLANG_NIF 1

#include <assert.h>
#include <bsp.h>
#include <erl_nif.h>
#include <errno.h>
#include <fcntl.h>
#include <rtems.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <dev/i2c/i2c.h>
#include <erl_nif.h>

#include <bsp.h>
#if defined LIBBSP_ARM_IMX_BSP_H
#define GRISP_I2C_REGISTER(path, alias) i2c_bus_register_imx((path), (alias))
#endif

/* NIF interface declarations */
int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info);
int upgrade(ErlNifEnv *env, void **priv_data, void **old_priv_data,
            ERL_NIF_TERM load_info);

static ERL_NIF_TERM am_ok;
static ERL_NIF_TERM am_error;

#define RAISE(msg)                                                             \
  enif_raise_exception(                                                        \
      env, enif_make_tuple2(env, am_error, enif_make_atom(env, msg)))
#define RAISE_TERM(msg, term)                                                  \
  enif_raise_exception(                                                        \
      env, enif_make_tuple3(env, am_error, enif_make_atom(env, msg), term))
#define RAISE_STRERROR(msg)                                                    \
  RAISE_TERM(msg, enif_make_string(env, strerror(errno), ERL_NIF_LATIN1))

static ErlNifResourceType *i2c_data;

typedef struct {
  int fd;
} grisp_i2c_data;

static void i2c_data_dtor(ErlNifEnv *env, void *obj) {
  close(((grisp_i2c_data *)obj)->fd);
}

int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info) {
  i2c_data = enif_open_resource_type(env, NULL, "i2c_data",
                                     (ErlNifResourceDtor *)i2c_data_dtor,
                                     ERL_NIF_RT_CREATE, NULL);
  assert(i2c_data != NULL);

  am_ok = enif_make_atom(env, "ok");
  am_error = enif_make_atom(env, "error");

  return 0;
}

int upgrade(ErlNifEnv *env, void **priv_data, void **old_priv_data,
            ERL_NIF_TERM load_info) {
  return 0;
}

static ERL_NIF_TERM register_bus_nif(ErlNifEnv *env, int argc,
                                     const ERL_NIF_TERM argv[]) {
  ErlNifBinary bus, alias;
  int rv;

  if (!enif_inspect_iolist_as_binary(env, argv[0], &bus)) {
    return RAISE_TERM("invalid_bus", argv[0]);
  }
  if (!enif_inspect_iolist_as_binary(env, argv[1], &alias)) {
    return RAISE_TERM("invalid_alias", argv[1]);
  }

  rv = i2c_bus_register_imx((char *)bus.data, (char *)alias.data);
  if (rv != 0) {
    return RAISE_STRERROR("register_failed");
  }

  return am_ok;
}

static ERL_NIF_TERM open_nif(ErlNifEnv *env, int argc,
                             const ERL_NIF_TERM argv[]) {
  ERL_NIF_TERM ret;
  ErlNifBinary bus;
  grisp_i2c_data *data;

  if (!enif_inspect_iolist_as_binary(env, argv[0], &bus)) {
    return RAISE_TERM("invalid_bus", argv[0]);
  }

  data = enif_alloc_resource(i2c_data, sizeof(grisp_i2c_data));

  data->fd = open((char *)bus.data, O_RDWR);

  if (data->fd < 0) {
    return RAISE_STRERROR("bus_open_failed");
  }

  ret = enif_make_resource(env, data);
  enif_release_resource(data);
  return ret;
}

static ERL_NIF_TERM transfer_nif(ErlNifEnv *env, int argc,
                                 const ERL_NIF_TERM argv[]) {
  grisp_i2c_data *data;
  unsigned int nmsgs;
  int rv;
  ERL_NIF_TERM head, tail;
  int arity = -1;
  const ERL_NIF_TERM *elems;
  ERL_NIF_TERM list;
  int i = 0;
  i2c_msg *msgs;
  ERL_NIF_TERM resps, rev_resps, resp;
  unsigned int chip_addr, flags, len;
  ErlNifBinary buf;
  uint8_t *readbuf;

  if (!enif_get_resource(env, argv[0], i2c_data, (void **)&data)) {
    return RAISE_TERM("invalid_fd", argv[0]);
  }
  if (!enif_get_list_length(env, argv[1], &nmsgs)) {
    return RAISE_TERM("invalid_message_list", argv[1]);
  }
  if (nmsgs <= 0) {
    return enif_make_list(env, 0);
  }

  msgs = (i2c_msg *)calloc(nmsgs, sizeof(i2c_msg));
  resps = enif_make_list(env, 0);

  struct i2c_rdwr_ioctl_data payload = {
      .msgs = msgs,
      .nmsgs = nmsgs,
  };

  list = argv[1];
  while (enif_get_list_cell(env, list, &head, &tail)) {
    if (!enif_get_tuple(env, head, &arity, &elems) || arity != 3) {
      return RAISE_TERM("invalid_message", head);
    }
    if (!enif_get_uint(env, elems[0], &chip_addr) || chip_addr > UINT16_MAX) {
      return RAISE_TERM("invalid_message_addr", head);
    }
    if (!enif_get_uint(env, elems[1], &flags) || flags > UINT16_MAX) {
      return RAISE_TERM("invalid_message_flags", head);
    }

    msgs[i].addr = (uint16_t)chip_addr;
    msgs[i].flags = (uint16_t)flags;

    if (flags & I2C_M_RD) {
      // Read
      if (!enif_get_uint(env, elems[2], &len) || len > UINT16_MAX) {
        return RAISE_TERM("invalid_message_len", head);
      }
      readbuf = enif_make_new_binary(env, len, &resp);
      msgs[i].buf = readbuf;
      msgs[i].len = (uint16_t)len;
    } else {
      // Write
      if (!enif_inspect_iolist_as_binary(env, elems[2], &buf) || buf.size < 0) {
        return RAISE_TERM("invalid_message_buf", head);
      }
      msgs[i].buf = buf.data;
      msgs[i].len = (uint16_t)buf.size;

      resp = am_ok;
    }

    resps = enif_make_list_cell(env, resp, resps);

    list = tail;
    i++;
  }

  rv = ioctl(data->fd, I2C_RDWR, &payload);
  free(msgs);
  if (rv != 0) {
    return enif_make_tuple3(
        env, am_error, enif_make_atom(env, "ioctl_failed"),
        enif_make_string(env, strerror(errno), ERL_NIF_LATIN1));
  }

  if (!enif_make_reverse_list(env, resps, &rev_resps)) {
    return RAISE_TERM("reverse_failed", resps);
  }
  return rev_resps;
}

static ErlNifFunc nif_funcs[] = {{"register_bus_nif", 2, register_bus_nif},
                                 {"open_nif", 1, open_nif},
                                 {"transfer_nif", 2, transfer_nif}};

ERL_NIF_INIT(grisp_ni2c, nif_funcs, &load, NULL, &upgrade, NULL)
