/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief common APIs for different RF frontend device
 */
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "common_lib.h"
#include "assertions.h"
#include "common/utils/load_module_shlib.h"
#include "common/utils/LOG/log.h"
#include "executables/softmodem-common.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/cmake_defs.h"
#include "openair1/PHY/TOOLS/tools_defs.h"

#define MAX_GAP 100ULL
const char *const devtype_names[MAX_RF_DEV_TYPE] =
    {"", "USRP B200", "USRP X300", "USRP N300", "USRP X400", "BLADERF", "LMSSDR", "IRIS", "No HW", "UEDv2", "RFSIMULATOR"};

const char *get_devname(int devtype) {
  if (devtype < MAX_RF_DEV_TYPE && devtype !=MIN_RF_DEV_TYPE )
    return devtype_names[devtype];
  return "none";
}

static int set_device(openair0_device_t *device)
{
  char *dev_type = device->host_type == RAU_HOST ? "RAU" : "RRU";
  const char *devname = get_devname(device->type);
  if (strcmp(devname, "none") != 0) {
    LOG_I(HW, "[%s] has loaded %s device.\n", dev_type, devname);
    return 0;
  }
  LOG_E(HW, "[%s] invalid HW device.\n", dev_type);
  return -1;
}

static int set_transport(openair0_device_t *device)
{
  char *dev_type = device->host_type == RAU_HOST ? "RAU" : "RRU";
  switch (device->transp_type) {
    case ETHERNET_TP:
      LOG_I(HW, "[%s] has loaded ETHERNET trasport protocol.\n", dev_type);
      return 0;

    case NONE_TP:
      LOG_I(HW, "[%s] has not loaded a transport protocol.\n", dev_type);
      return 0;

    default:
      LOG_E(HW, "[%s] invalid transport protocol.\n", dev_type);
      return -1;
  }
}

typedef int (*devfunc_t)(openair0_device_t *, openair0_config_t *, eth_params_t *);

/* look for the interface library and load it */
int load_lib(openair0_device_t *device, openair0_config_t *openair0_cfg, eth_params_t *eth_cfg, rau_type_t rau_type)
{
  openair0_cfg->command_line_sample_advance = get_softmodem_params()->command_line_sample_advance;

  openair0_cfg->recplay_mode = read_recplayconfig(&openair0_cfg->recplay_conf, &device->recplay_state);
  // softmodem has to know we use the iqrecorder to workaround randomized algorithms
  IS_SOFTMODEM_IQRECORDER = openair0_cfg->recplay_mode == RECPLAY_RECORDMODE;

  char *deflibname = OAI_RF_LIBNAME;
  loader_shlibfunc_t shlib_fdesc = {.fname = "device_init"};
  if (openair0_cfg->recplay_mode == RECPLAY_REPLAYMODE) {
    deflibname = OAI_IQPLAYER_LIBNAME;
    IS_SOFTMODEM_IQPLAYER = true; // softmodem has to know we use the iqplayer to workaround randomized algorithms
  } else {
    switch (rau_type) {
      case RAU_LOCAL_RADIO_HEAD:
        if (IS_SOFTMODEM_RFSIM)
          deflibname = OAI_RFSIM_LIBNAME;
        break;
      case RAU_REMOTE_THIRDPARTY_RADIO_HEAD:
        deflibname = OAI_THIRDPARTY_TP_LIBNAME;
        shlib_fdesc.fname = "transport_init";
        break;
      case RAU_REMOTE_RADIO_HEAD:
        deflibname = OAI_TP_LIBNAME;
        shlib_fdesc.fname = "transport_init";
        break;
      default:
        AssertFatal(false, "impossible radio head\n");
    }
  }

  char *devname = NULL;
  paramdef_t device_params = {"name", CONFIG_HLP_DEVICE, 0, .strptr = &devname, .defstrval = deflibname, TYPE_STRING, 0};
  config_get(config_get_if(), &device_params, 1, DEVICE_SECTION);

  int ret = load_module_shlib(devname, &shlib_fdesc, 1, NULL);
  if (devname && devname != deflibname) {
    free(devname);
    devname = NULL;
  }
  AssertFatal(ret >= 0, "Library %s couldn't be loaded\n", devname);
  return ((devfunc_t)shlib_fdesc.fptr)(device, openair0_cfg, eth_cfg);
}

void *create_ring(int sz_bytes) {
  AssertFatal(sz_bytes % PAGE_SIZE == 0, "must be a number of pages %d", sz_bytes);
  // get a temporary file fd
  int fd = fileno(tmpfile());
  if (fd == -1) {
      AssertFatal(false, "tmpfile failed: %s\n", strerror(errno));
  }
  // set it's size appropriately. We need exactly `sz` bytes as underlying memory
  if (ftruncate(fd, sz_bytes) != 0) {
      close(fd);
      AssertFatal(false, "ftruncate failed: %s\n", strerror(errno));
  }

  void *ret = mmap(NULL, 2 * sz_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ret == MAP_FAILED) {
      close(fd);
      AssertFatal(false, "mmap reserve failed: %s\n", strerror(errno));
  }

  if (mmap(ret, sz_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != ret) {
      munmap(ret, 2 * sz_bytes);
      close(fd);
      AssertFatal(false, "mmap first half failed: %s\n", strerror(errno));
  }

  if (mmap(ret + sz_bytes, sz_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != ret + sz_bytes) {
      munmap(ret, 2 * sz_bytes);
      close(fd);
      AssertFatal(false, "mmap second half failed: %s\n", strerror(errno));
  }
  close(fd);
  return ret;
}

static void init_reorder(re_order_t *r, openair0_config_t *openair0_cfg)
{
  pthread_mutex_init(&r->mutex_store, NULL);
  pthread_mutex_init(&r->mutex_write, NULL);

  r->sz = ceil_mod(openair0_cfg->sample_rate / 100, PAGE_SIZE / sizeof(int)); // 10 ms storage
  r->grain = 2048; // arbitrary read size, chosen to fit in a ethernet jumbo frame
  r->nb_writers = create_ring(r->sz * sizeof(*r->nb_writers));

  r->ring = calloc(openair0_cfg->tx_num_channels, sizeof(*r->ring));
  for (int i = 0; i < openair0_cfg->tx_num_channels; i++)
    r->ring[i] = create_ring(r->sz * sizeof(c16_t));

  r->initDone = false;
  r->nextTS = 0;
  r->end = 0;
  r->ts_per_writer = NULL;
  r->consumer_args = NULL;
  r->consumer_thread = 0;
  r->sample_timestamps = create_ring(r->sz * sizeof(openair0_timestamp_t));
  memset(r->sample_timestamps, 0, r->sz * sizeof(openair0_timestamp_t));
  pthread_cond_init(&r->cond_data_available, NULL);
  pthread_cond_init(&r->cond_space_available, NULL);
  r->last_ts_per_ue = NULL;
  r->max_nb_writers = 0;
}

void *reorder_consumer_thread(void *arg) {
  reorder_consumer_args_t *args = (reorder_consumer_args_t *)arg;
  openair0_device_t *device = args->device;
  re_order_t *ctx = args->ctx;
  int nbAnt = args->nbAnt;
  int grain = ctx->grain;

  while (!args->stop) {
    pthread_mutex_lock(&ctx->mutex_store);
    while (!args->stop && (!ctx->initDone || ctx->end <= ctx->nextTS)) {
      pthread_cond_wait(&ctx->cond_data_available, &ctx->mutex_store);
    }
    if (args->stop || !ctx->initDone) {
      pthread_mutex_unlock(&ctx->mutex_store);
      break;
    }

    uint64_t ts = ctx->nextTS;
    if (ctx->nb_writers[ts % ctx->sz] >= nbAnt) {
      openair0_timestamp_t original_ts = ((openair0_timestamp_t *)ctx->sample_timestamps)[ts % ctx->sz];

      void *ptr[nbAnt];
      for (int a = 0; a < nbAnt; a++) {
        ptr[a] = ((c16_t *)ctx->ring[a]) + (ts % ctx->sz);
      }

      int nsamps_to_process = min(grain, (int)(ctx->end - ctx->nextTS));
      ctx->nextTS += nsamps_to_process;

      pthread_cond_signal(&ctx->cond_space_available);
      pthread_mutex_unlock(&ctx->mutex_store);

      if (args->nrue_ru_write) {
        args->nrue_ru_write(NULL, original_ts, ptr, nsamps_to_process, nbAnt, 0);
      } else if (device) {
        device->trx_write_func(device, original_ts + device->firstTS, ptr, nsamps_to_process, nbAnt, 0);
      }

      pthread_mutex_lock(&ctx->mutex_store);
      for (int a = 0; a < nbAnt; a++) {
        memset(((c16_t *)ctx->ring[a]) + (ts % ctx->sz), 0, nsamps_to_process * sizeof(c16_t));
      }
      memset(ctx->nb_writers + (ts % ctx->sz), 0, nsamps_to_process * sizeof(*ctx->nb_writers));
      pthread_mutex_unlock(&ctx->mutex_store);
    } else {
      LOG_E(HW, "Missing data at ts=%lu (end=%lu, writers=%d/%d)\n",
            ts, ctx->end, ctx->nb_writers[ts % ctx->sz], nbAnt);
      pthread_mutex_unlock(&ctx->mutex_store);
      continue;
    }
  }
  return NULL;
}

int openair0_device_load(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  int rc=0;
  rc=load_lib(device, openair0_cfg, NULL,RAU_LOCAL_RADIO_HEAD );

  if ( rc >= 0) {
    if ( set_device(device) < 0) {
      LOG_E(HW, "%s %d:Unsupported radio head\n", __FILE__, __LINE__);
      return -1;
    }
  } else {
    AssertFatal(false, "can't open the radio device: %s\n", get_devname(device->type));
  }
  init_reorder(&device->reOrder, openair0_cfg);
  re_order_t *ctx = &device->reOrder;
  reorder_consumer_args_t *args = malloc(sizeof(*args));
  args->ctx = ctx;
  args->device = device;
  args->nrue_ru_write = NULL;
  args->nbAnt = openair0_cfg->tx_num_channels;
  args->stop = false;

  if (pthread_create(&ctx->consumer_thread, NULL, reorder_consumer_thread, args) != 0) {
    free(args);
    LOG_E(HW, "Failed to create consumer thread\n");
    return -1;
  }
  ctx->consumer_args = args;
  return rc;
}

int openair0_transport_load(openair0_device_t *device, openair0_config_t *openair0_cfg, eth_params_t *eth_params)
{
  int rc = load_lib(device, openair0_cfg, eth_params, RAU_REMOTE_RADIO_HEAD);

  if ( rc >= 0) {
    if ( set_transport(device) < 0) {
      LOG_E(HW, "%s %d:Unsupported transport protocol\n", __FILE__, __LINE__);
      return -1;
    }
  }

  return rc;
}

int openair0_load(openair0_device_t *device, char *name, openair0_config_t *openair0_cfg, eth_params_t *eth_params)
{
  loader_shlibfunc_t shlib_fdesc[1];
  int ret = 0;

  shlib_fdesc[0].fname = eth_params == NULL ? "device_init" : "transport_init";

  ret = load_module_shlib(name, shlib_fdesc, 1, NULL);
  AssertFatal((ret >= 0), "Library %s couldn't be loaded\n", name);
  return ((devfunc_t)shlib_fdesc[0].fptr)(device, openair0_cfg, eth_params);
}

// mutex (or atomic flags) will be mandatory because this out order system root cause is there are several writer threads
int openair0_write_reorder_common(nrue_ru_write_t nrue_ru_write,
                                  PHY_VARS_NR_UE *UE,
                                  openair0_device_t *device,
                                  openair0_timestamp_t timestamp,
                                  void **txp,
                                  int nsamps,
                                  int nb_writers,
                                  int nbAnt,
                                  int flags)
{
  re_order_t *ctx = &device->reOrder;

  pthread_mutex_lock(&ctx->mutex_store);
  if (!ctx->initDone) {
    ctx->nextTS = timestamp;
    ctx->end = timestamp;
    if (nb_writers > 0) {
      ctx->ts_per_writer = calloc(nb_writers, sizeof(*ctx->ts_per_writer));
    } else {
      LOG_W(HW, "nb_writers is 0! Using fallback size 1\n");
      ctx->ts_per_writer = calloc(1, sizeof(*ctx->ts_per_writer));
    }
    ctx->initDone = true;
    ctx->refcount = 1;
  } else {
    ctx->refcount++;
  }

  while (ctx->end - ctx->nextTS >= ctx->sz) {
    pthread_cond_wait(&ctx->cond_space_available, &ctx->mutex_store);
  }

  int write_buff_index = timestamp % ctx->sz;
  for (int a = 0; a < nbAnt; a++) {
    c16adds(txp[a], ((c16_t *)ctx->ring[a]) + write_buff_index,
            ((c16_t *)ctx->ring[a]) + write_buff_index, nsamps);
  }
  const int *endl = ctx->nb_writers + write_buff_index + nsamps;
  for (int *i = ctx->nb_writers + write_buff_index; i < endl; i++) *i = *i + 1;
  openair0_timestamp_t *ts_ptr = (openair0_timestamp_t *)ctx->sample_timestamps + write_buff_index;
  int remaining = nsamps;
  openair0_timestamp_t current_ts = timestamp;
  while (remaining > 0) {
    *ts_ptr++ = current_ts++;
    remaining--;
    if (ts_ptr >= (openair0_timestamp_t *)ctx->sample_timestamps + ctx->sz) {
      ts_ptr = (openair0_timestamp_t *)ctx->sample_timestamps;
    }
  }

  uint64_t max_possible_end = timestamp + nsamps;
  int guard = 0;
  while (ctx->end < max_possible_end && guard < ctx->sz) {
    if (ctx->nb_writers[ctx->end % ctx->sz] >= nb_writers) {
      ctx->end++;
      guard++;
    } else {
      break;
    }
  }
  if (ctx->end > ctx->nextTS) {
    pthread_cond_signal(&ctx->cond_data_available);
  }
  pthread_mutex_unlock(&ctx->mutex_store);

  return nsamps;
}

int openair0_write_reorder(openair0_device_t *device, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags)
{
  return openair0_write_reorder_common(NULL, NULL, device, timestamp, txp, nsamps, 1, nbAnt, flags);
}

void openair0_write_reorder_clear_context(openair0_device_t *device) {
  LOG_W(HW, "received write reorder clear context\n");
  re_order_t *ctx = &device->reOrder;

  pthread_mutex_lock(&ctx->mutex_store);

  if (!ctx->initDone) {
    pthread_mutex_unlock(&ctx->mutex_store);
    return;
  }

  ctx->refcount--;
  if (ctx->refcount == 0) {
    pthread_t thread_to_join = ctx->consumer_thread;
    reorder_consumer_args_t *args_to_free = ctx->consumer_args;

    if (ctx->consumer_args) {
      ctx->consumer_args->stop = true;
      pthread_cond_signal(&ctx->cond_data_available);
      pthread_cond_signal(&ctx->cond_space_available);
    }
    ctx->consumer_args = NULL;
    ctx->consumer_thread = 0;
    ctx->initDone = false;

    if (ctx->ts_per_writer) {
      free(ctx->ts_per_writer);
      ctx->ts_per_writer = NULL;
    }
    if (ctx->sample_timestamps) {
      munmap(ctx->sample_timestamps, ctx->sz * sizeof(openair0_timestamp_t));
      ctx->sample_timestamps = NULL;
    }
    if (ctx->nb_writers) {
      munmap(ctx->nb_writers, ctx->sz * sizeof(*ctx->nb_writers));
      ctx->nb_writers = NULL;
    }
    if (ctx->ring) {
      for (int i = 0; i < device->openair0_cfg->tx_num_channels; i++) {
        if (ctx->ring[i]) {
          munmap(ctx->ring[i], ctx->sz * sizeof(c16_t));
        }
      }
      free(ctx->ring);
      ctx->ring = NULL;
    }
    if (ctx->last_ts_per_ue) {
      free(ctx->last_ts_per_ue);
      ctx->last_ts_per_ue = NULL;
    }

    pthread_mutex_unlock(&ctx->mutex_store);

    if (thread_to_join != 0) {
      pthread_join(thread_to_join, NULL);
    }
    if (args_to_free) {
      free(args_to_free);
    }
  } else {
    pthread_mutex_unlock(&ctx->mutex_store);
  }
}
