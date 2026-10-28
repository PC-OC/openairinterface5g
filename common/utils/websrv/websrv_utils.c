/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief: utility functions for all websrv back-end sources
 */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include "common/utils/LOG/log.h"
#include <libgen.h>
#include <jansson.h>
#include <ulfius.h>
#include "common/utils/telnetsrv/telnetsrv.h"

extern telnetsrv_params_t *get_telnetsrv_params(void);

/* websrv_printf_t is an internal structure storing messages while processing a request */
/* The message is used to fill a response body                                           */
typedef struct websrv_printf_t {
  pthread_mutex_t mutex; // protect the message between the websrv_printf_start and websrv_print_end calls
  struct _u_response *response; // the ulfius response structure, used to send the message
  char *buff; // a buffer to store the message, allocated in websrv_printf_start, free in websrv_print_end
  int buffsize; //
  char *buffptr; // pointer to free portion of buff
  bool async; // the buffer will be used to print from another thread than the callback (command pushed in a tpool queue)
  bool abandoned; // true if this state was abandoned (replaced by a new request without proper cleanup)
  volatile int refcount; // reference count for thread-safe state management
} websrv_printf_t;

/*
 * RAII-style helper macro for safe websrv state access
 * Usage:
 *   WITH_WEBSRV_STATE(state, {
 *     // use state here
 *   });
 */
#define WITH_WEBSRV_STATE(state_var, code_block) \
  do { \
    telnetsrv_params_t *tparams = get_telnetsrv_params(); \
    websrv_printf_t *state_var = NULL; \
    if (tparams) { \
      pthread_mutex_lock(&tparams->websrv_callback_mutex); \
      state_var = tparams->websrv_printf_state; \
      if (state_var) { \
        __atomic_fetch_add(&state_var->refcount, 1, __ATOMIC_SEQ_CST); \
      } \
      pthread_mutex_unlock(&tparams->websrv_callback_mutex); \
    } \
    if (state_var == NULL) { \
      LOG_E(UTIL, "[websrv] NULL state in %s\n", __FUNCTION__); \
    } else { \
      code_block \
      websrv_release_state(state_var); \
    } \
  } while (0)

static inline bool websrv_state_valid(websrv_printf_t *state)
{
  if (state == NULL) {
    return false;
  }
  return !state->abandoned;
}

/*--------------------- functions for help ------------------------*/
/* possibly build a help file to be downloaded by the frontend */
void websrv_utils_build_hlpfile(char *fname)
{
  char *info = NULL;

  if (strstr(fname, "core.html") != NULL) {
    long number_of_processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (number_of_processors > 0) {
      char *pinfo = malloc(255);
      snprintf(pinfo, 254, "Available cores on this system: 0 to %d", (int)number_of_processors - 1);
      info = pinfo;
    }
  }
  if (info != NULL) {
    FILE *f = fopen(fname, "w");
    if (f == NULL) {
      LOG_I(UTIL, "[websrv]: Help file %s couldn't be created\n", fname);
    } else {
      fprintf(f, "%s", info);
      fclose(f);
    }
    free(info);
  }
}

void websrv_utils_build_hlpfiles(char *path)
{
  char fname[255];
  snprintf(fname, sizeof(fname), "%s/helpfiles/softmodem_show_threadsched_core.html", path);
  websrv_utils_build_hlpfile(fname);
}
int websrv_utils_testhlp(char *dir, char *module, char *cmdtitle, char *coltitle)
{
  char fname[255];

  snprintf(fname, sizeof(fname) - 1, "%s/helpfiles/%s_%s_%s.html", dir, module, cmdtitle, coltitle);
  for (int i = 0; i < strlen(fname); i++) {
    if (fname[i] == ' ')
      fname[i] = '_';
  }
  int st = access(fname, R_OK);
  return ((st == 0) ? 1 : 0);
}
/*-----------------------------------------------------------*/
/* functions http request/response helpers                  */
/* dump a json objects */
void websrv_printjson(char *label, json_t *jsonobj, int dbglvl)
{
  if (dbglvl > 0) {
    char *jstr = json_dumps(jsonobj, 0);
    LOG_I(UTIL, "[websrv] %s:%s\n", label, (jstr == NULL) ? "??\n" : jstr);
    free(jstr);
  }
}

/*------------------------------------------------------------------------------*/
/* dump a request                                                               */
void websrv_dump_request(char *label, const struct _u_request *request, int dbglvl)
{
  if (dbglvl > 0) {
    LOG_I(UTIL, "[websrv] %s, request %s, proto %s, verb %s, path %s\n", label, request->http_url, request->http_protocol, request->http_verb, request->http_verb);
    if (request->map_post_body != NULL)
      for (int i = 0; i < u_map_count(request->map_post_body); i++)
        LOG_I(UTIL, "[websrv] POST parameter %i %s : %s\n", i, u_map_enum_keys(request->map_post_body)[i], u_map_enum_values(request->map_post_body)[i]);
    if (request->map_cookie != NULL)
      for (int i = 0; i < u_map_count(request->map_cookie); i++)
        LOG_I(UTIL, "[websrv] cookie variable %i %s : %s\n", i, u_map_enum_keys(request->map_cookie)[i], u_map_enum_values(request->map_cookie)[i]);
    if (request->map_header != NULL)
      for (int i = 0; i < u_map_count(request->map_header); i++)
        LOG_I(UTIL, "[websrv] header variable %i %s : %s\n", i, u_map_enum_keys(request->map_header)[i], u_map_enum_values(request->map_header)[i]);
    if (request->map_url != NULL)
      for (int i = 0; i < u_map_count(request->map_url); i++)
        LOG_I(UTIL, "[websrv] url variable %i %s : %s\n", i, u_map_enum_keys(request->map_url)[i], u_map_enum_values(request->map_url)[i]);
  }
}
/*-----------------------------------*/
/* build a json body in a response   */
void websrv_jbody(struct _u_response *response, json_t *jbody, int httpstatus, int dbglvl)
{
  websrv_printjson((char *)__FUNCTION__, jbody, dbglvl);
  int us = ulfius_add_header_to_response(response, "content-type", "application/json");
  if (us != U_OK) {
    ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_add_header_to_response)");
    LOG_E(UTIL, "[websrv] cannot set response header type ulfius error %d \n", us);
  }
  us = ulfius_set_json_body_response(response, httpstatus, jbody);
  if (us != U_OK) {
    ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_set_json_body_response)");
    LOG_E(UTIL, "[websrv] cannot set body response ulfius error %d \n", us);
  }
  return;
}

/*----------------------------------------------------------------*/
/* format a json string array in a response body                  */
int websrv_string_response(char *astring, struct _u_response *response, int httpstatus, int dbglvl)
{
  json_t *jstr = json_array();
  char *tokctx;
  char *aline = strtok_r(astring, "\n", &tokctx);
  while (aline != NULL) {
    json_t *jline = json_string(aline);
    json_array_append_new(jstr, jline);
    aline = strtok_r(NULL, "\n", &tokctx);
  }
  json_t *jbody = json_pack("{s:o}", "display", jstr);
  websrv_jbody(response, jbody, httpstatus, dbglvl);
  return 0;
}
/*----------------------------------------------------------------------------------*/
/* set of calls to fill a buffer with a string and  use this buffer in a response */
int websrv_printf_start(struct _u_response *response, int buffsize, bool async, websrv_printf_t **state_out)
{
  websrv_printf_t *state = calloc(1, sizeof(*state));
  if (state == NULL) {
    LOG_E(UTIL, "[websrv] calloc failed in websrv_printf_start\n");
    return -1;
  }

  pthread_mutexattr_t attr;
  int rc = pthread_mutexattr_init(&attr);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutexattr_init failed in websrv_printf_start: %s\n", strerror(rc));
    free(state);
    return -1;
  }

  rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutexattr_settype failed in websrv_printf_start: %s\n", strerror(rc));
    pthread_mutexattr_destroy(&attr);
    free(state);
    return -1;
  }

  rc = pthread_mutex_init(&state->mutex, &attr);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_init failed in websrv_printf_start: %s\n", strerror(rc));
    pthread_mutexattr_destroy(&attr);
    free(state);
    return -1;
  }
  pthread_mutexattr_destroy(&attr);

  state->buff = calloc(1, buffsize);
  if (state->buff == NULL) {
    LOG_E(UTIL, "[websrv] calloc failed for buffer in websrv_printf_start\n");
    pthread_mutex_destroy(&state->mutex);
    free(state);
    return -1;
  }

  state->buffptr = state->buff;
  state->buffsize = buffsize;
  state->response = response;
  state->async = async;
  state->abandoned = false;
  state->refcount = 1;

  telnetsrv_params_t *tparams = get_telnetsrv_params();
  if (tparams == NULL) {
    LOG_E(UTIL, "[websrv] get_telnetsrv_params returned NULL in %s\n", __FUNCTION__);
    pthread_mutex_destroy(&state->mutex);
    free(state->buff);
    free(state);
    return -1;
  }
  pthread_mutex_lock(&tparams->websrv_callback_mutex);
  if (tparams->websrv_printf_state != NULL) {
    websrv_printf_t *old_state = tparams->websrv_printf_state;
    pthread_mutex_lock(&old_state->mutex);
    old_state->abandoned = true;
    pthread_mutex_unlock(&old_state->mutex);
    LOG_W(UTIL, "[websrv] Previous state was abandoned (missing websrv_printf_end call?)\n");
    // Don't destroy/free here - let reference counting handle cleanup
    // The old state will be automatically cleaned up when its refcount reaches 0
  }
  tparams->websrv_printf_state = state;
  pthread_mutex_unlock(&tparams->websrv_callback_mutex);

  if (state_out != NULL) {
    *state_out = state;
  }
  return 0;
}

websrv_printf_t *websrv_get_state_safe(void)
{
  telnetsrv_params_t *tparams = get_telnetsrv_params();
  if (tparams == NULL) {
    LOG_E(UTIL, "[websrv] get_telnetsrv_params returned NULL in %s\n", __FUNCTION__);
    return NULL;
  }

  pthread_mutex_lock(&tparams->websrv_callback_mutex);
  websrv_printf_t *state = tparams->websrv_printf_state;
  if (state != NULL) {
    __atomic_fetch_add(&state->refcount, 1, __ATOMIC_SEQ_CST);
  }
  pthread_mutex_unlock(&tparams->websrv_callback_mutex);
  return state;
}

void websrv_release_state(websrv_printf_t *state)
{
  if (state == NULL) {
    return;
  }

  if (__atomic_fetch_sub(&state->refcount, 1, __ATOMIC_SEQ_CST) == 1) {
    telnetsrv_params_t *tparams = get_telnetsrv_params();
    if (tparams != NULL) {
      pthread_mutex_lock(&tparams->websrv_callback_mutex);
      if (tparams->websrv_printf_state == state) {
        tparams->websrv_printf_state = NULL;
      }
      pthread_mutex_unlock(&tparams->websrv_callback_mutex);
    }

    pthread_mutex_destroy(&state->mutex);
    free(state->buff);
    free(state);
  }
}

void websrv_printf_atpos(int pos, const char *message, ...)
{
  websrv_printf_t *state = websrv_get_state_safe();
  if (state == NULL) {
    LOG_E(UTIL, "[websrv] websrv_printf_atpos called with NULL state in %s\n", __FUNCTION__);
    return;
  }

  int rc = pthread_mutex_lock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_lock failed in %s: %s\n", __FUNCTION__, strerror(rc));
    websrv_release_state(state);
    return;
  }

  va_list va_args;
  va_start(va_args, message);

  int available = state->buffsize - pos - 1;
  if (available <= 0) {
    LOG_W(UTIL, "[websrv] Buffer overflow in websrv_printf_atpos at position %d\n", pos);
    va_end(va_args);
    rc = pthread_mutex_unlock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf_atpos: %s\n", strerror(rc));
    }
    return;
  }

  int written = vsnprintf(state->buff + pos, available, message, va_args);
  state->buffptr = state->buff + pos + written;

  if (state->buffptr < state->buff + state->buffsize) {
    *state->buffptr = '\0';
  } else {
    state->buffptr = state->buff + state->buffsize - 1;
    *state->buffptr = '\0';
    state->buffptr++;
  }

  va_end(va_args);

  rc = pthread_mutex_unlock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf_atpos: %s\n", strerror(rc));
  }
  websrv_release_state(state);
  return;
}

void websrv_printf(const char *message, ...)
{
  websrv_printf_t *state = websrv_get_state_safe();
  if (state == NULL) {
    LOG_E(UTIL, "[websrv] websrv_printf called with NULL state in %s\n", __FUNCTION__);
    return;
  }

  int rc = pthread_mutex_lock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_lock failed in %s: %s\n", __FUNCTION__, strerror(rc));
    websrv_release_state(state);
    return;
  }

  va_list va_args;
  va_start(va_args, message);
  int available = state->buffsize - (state->buffptr - state->buff) - 1;
  if (available <= 0) {
    LOG_W(UTIL, "[websrv] Buffer full in websrv_printf, cannot write more\n");
    va_end(va_args);
    rc = pthread_mutex_unlock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf: %s\n", strerror(rc));
    }
    websrv_release_state(state);
    return;
  }

  int written = vsnprintf(state->buffptr, available, message, va_args);

  if (written >= available) {
    LOG_W(UTIL, "[websrv] Buffer overflow in websrv_printf, truncating\n");
    state->buffptr = state->buff + state->buffsize - 1;
    *state->buffptr = '\0';
    state->buffptr = state->buff + state->buffsize;
  } else if (written > 0) {
    state->buffptr += written;
    *state->buffptr = '\0';  // Ensure null-termination
  }

  va_end(va_args);

  rc = pthread_mutex_unlock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf: %s\n", strerror(rc));
  }
  websrv_release_state(state);
  return;
}

void websrv_printf_end(int httpstatus, int dbglvl)
{
  websrv_printf_t *state = websrv_get_state_safe();
  if (state == NULL) {
    LOG_E(UTIL, "[websrv] websrv_printf_end called with NULL state in %s\n", __FUNCTION__);
    return;
  }

  int rc = pthread_mutex_lock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_lock failed in %s: %s\n", __FUNCTION__, strerror(rc));
    websrv_release_state(state);
    return;
  }

  if (state->abandoned) {
    rc = pthread_mutex_unlock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf_end: %s\n", strerror(rc));
    }
    websrv_release_state(state);
    return;
  }

  int count = 0;
  bool timeout_occurred = false;
  bool mutex_held = true;

  while (__atomic_load_n(&state->async, __ATOMIC_SEQ_CST)) {
    rc = pthread_mutex_unlock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf_end (async wait): %s\n", strerror(rc));
    }
    mutex_held = false;
    usleep(10000); // Wait 10ms per iteration
    count++;

    if (count >= 200) { // Wait up to 200 * 10ms = 2000ms
      __atomic_store_n(&state->async, 0, __ATOMIC_SEQ_CST);
      timeout_occurred = true;
      break;
    }

    rc = pthread_mutex_lock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_lock failed in websrv_printf_end (retry): %s\n", strerror(rc));
      websrv_release_state(state);
      return;
    }
    mutex_held = true;
  }

  if (timeout_occurred) {
    if (!mutex_held) {
      rc = pthread_mutex_lock(&state->mutex);
      if (rc != 0) {
        LOG_E(UTIL, "[websrv] pthread_mutex_lock failed in websrv_printf_end (timeout recovery): %s\n", strerror(rc));
        websrv_release_state(state);
        return;
      }
      mutex_held = true;
    }

    int remaining = state->buffsize - (state->buffptr - state->buff) - 1;
    if (remaining > 0) {
      int written = snprintf(state->buffptr, remaining, "No response from server...");
      if (written > 0) {
        state->buffptr += written;
      }
    }
  }

  if (httpstatus >= 200 && httpstatus < 300) {
    LOG_I(UTIL, "[websrv] %s\n", state->buff);
    websrv_string_response(state->buff, state->response, httpstatus, dbglvl);
  } else if (httpstatus < 1000) {
    LOG_W(UTIL, "[websrv] %s\n", state->buff);
    ulfius_set_binary_body_response(state->response, httpstatus, state->buff, state->buffptr - state->buff);
  }

  state->async = false;

  rc = pthread_mutex_unlock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_printf_end: %s\n", strerror(rc));
  }
  websrv_release_state(state);
}

void websrv_async_printf(const char *message, ...)
{
  websrv_printf_t *state = websrv_get_state_safe();
  if (state == NULL) {
    LOG_W(UTIL, "[websrv] websrv_async_printf called with NULL state in %s (no active web request)\n", __FUNCTION__);
    return;
  }

  int rc = pthread_mutex_trylock(&state->mutex);
  if (rc != 0) {
    LOG_W(UTIL, "[websrv] websrv_async_printf in %s: could not lock state mutex, skipping message\n", __FUNCTION__);
    websrv_release_state(state);
    return;
  }

  if (state->abandoned) {
    rc = pthread_mutex_unlock(&state->mutex);
    if (rc != 0) {
      LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_async_printf: %s\n", strerror(rc));
    }
    websrv_release_state(state);
    return;
  }

  if (__atomic_load_n(&state->async, __ATOMIC_SEQ_CST)) {
    va_list va_args;
    va_start(va_args, message);
    int available = state->buffsize - (state->buffptr - state->buff) - 1;
    int written = vsnprintf(state->buffptr, available, message, va_args);

    if (written >= available) {
      LOG_W(UTIL, "[websrv] Buffer overflow in websrv_async_printf, truncating\n");
      state->buffptr = state->buff + state->buffsize - 1;
      *state->buffptr = '\0';
      state->buffptr = state->buff + state->buffsize;
    } else if (written > 0) {
      state->buffptr += written;
      *state->buffptr = '\0';  // Ensure null-termination
    }

    va_end(va_args);
    __atomic_store_n(&state->async, 0, __ATOMIC_SEQ_CST);
  } else {
    LOG_D(UTIL, "[websrv] delayed websrv_async_printf skipped\n");
  }

  rc = pthread_mutex_unlock(&state->mutex);
  if (rc != 0) {
    LOG_E(UTIL, "[websrv] pthread_mutex_unlock failed in websrv_async_printf: %s\n", strerror(rc));
  }
  websrv_release_state(state);
  return;
}
/* */
/*----------------------------------------------------------------------------------------------------------*/
/* callbacks and utility functions to stream a file */
char *websrv_read_file(const char *filename)
{
  char *buffer = NULL;
  long length;
  struct stat statbuf;

  FILE *f = fopen(filename, "rb");
  int st = stat(filename, &statbuf);
  if (f != NULL && st == 0) {
    length = statbuf.st_size;
    buffer = malloc(length + 1);
    if (buffer != NULL) {
      int rlen = fread(buffer, 1, length, f);
      if (rlen != length) {
        free(buffer);
        LOG_E(UTIL, "[websrv] couldn't read %s_\n", filename);
        fclose(f);
        return NULL;
      }
      buffer[length] = '\0';
    }
  } else {
    LOG_E(UTIL, "[websrv] error accessing %s: %s\n", filename, strerror(errno));
  }
  if (f != NULL)
    fclose(f);
  return buffer;
}
/* callbacks to send static streams */
static ssize_t callback_stream(void *cls, uint64_t pos, char *buf, size_t max)
{
  if (cls != NULL) {
    return fread(buf, sizeof(char), max, (FILE *)cls);
  } else {
    return U_STREAM_END;
  }
}

static void callback_stream_free(void *cls)
{
  if (cls != NULL) {
    fclose((FILE *)cls);
  }
}

FILE *websrv_getfile(char *filename, struct _u_response *response)
{
  FILE *f = fopen(filename, "rb");
  int length;

  if (f) {
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    LOG_I(UTIL, "[websrv] sending %d bytes from %s\n", length, filename);
  } else {
    LOG_E(UTIL, "[websrv] couldn't open %s\n", filename);
    return NULL;
  }

  char *content_type = "text/html";
  size_t nl = strlen(filename);
  if (nl >= 3 && !strcmp(filename + nl - 3, "css"))
    content_type = "text/css";
  if (nl >= 2 && !strcmp(filename + nl - 2, "js"))
    content_type = "text/javascript";

  int ust = ulfius_add_header_to_response(response, "content-type", content_type);
  if (ust != U_OK) {
    ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
    LOG_E(UTIL, "[websrv] cannot set response header type ulfius error %d \n", ust);
    fclose(f);
    return NULL;
  }

  ust = ulfius_set_stream_response(response, 200, callback_stream, callback_stream_free, length, 1024, f);
  if (ust != U_OK) {
    LOG_E(UTIL, "[websrv] ulfius_set_stream_response error %d\n", ust);
    fclose(f);
    return NULL;
  }
  return f;
}
/* */

