/* MIT License
 *
 * Copyright (c) 2024 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares_setup.h"
#include "ares.h"
#include "ares_private.h"
#include "ares_event.h"

#ifdef __ANDROID__

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
  /* No ability */
  return ARES_ENOTIMP;
}

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
  /* No-op */
}

#elif defined(__linux__)

#  include <sys/inotify.h>

struct ares_event_configchg {
  int                  inotify_fd;
  ares_event_thread_t *e;
};

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
  if (configchg == NULL) {
    return;
  }

  /* Tell event system to stop monitoring for changes.  This will cause the
   * cleanup to be called */
  ares_event_update(NULL, configchg->e, ARES_EVENT_FLAG_NONE, NULL,
                    configchg->inotify_fd, NULL, NULL, NULL);
}

static void ares_event_configchg_free(void *data)
{
  ares_event_configchg_t *configchg = data;
  if (configchg == NULL) {
    return;
  }

  if (configchg->inotify_fd >= 0) {
    close(configchg->inotify_fd);
    configchg->inotify_fd = -1;
  }

  ares_free(configchg);
}

static void ares_event_configchg_cb(ares_event_thread_t *e, ares_socket_t fd,
                                    void *data, ares_event_flags_t flags)
{
  const ares_event_configchg_t *configchg = data;

  /* Some systems cannot read integer variables if they are not
   * properly aligned. On other systems, incorrect alignment may
   * decrease performance. Hence, the buffer used for reading from
   * the inotify file descriptor should have the same alignment as
   * struct inotify_event. */
  unsigned char                 buf[4096]
    __attribute__((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  ssize_t                     len;
  ares_bool_t                 triggered = ARES_FALSE;

  (void)fd;
  (void)flags;

  while (1) {
    const unsigned char *ptr;

    len = read(configchg->inotify_fd, buf, sizeof(buf));
    if (len <= 0) {
      break;
    }

    /* Loop over all events in the buffer. Says kernel will check the buffer
     * size provided, so I assume it won't ever return partial events. */
    for (ptr  = buf; ptr < buf + len;
         ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event *)ptr;

      if (event->len == 0 || ares_strlen(event->name) == 0) {
        continue;
      }

      if (strcasecmp(event->name, "resolv.conf") == 0 ||
          strcasecmp(event->name, "nsswitch.conf") == 0) {
        triggered = ARES_TRUE;
      }
    }
  }

  /* Only process after all events are read.  No need to process more often as
   * we don't want to reload the config back to back */
  if (triggered) {
    ares_reinit(e->channel);
  }
}

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
  ares_status_t           status = ARES_SUCCESS;
  ares_event_configchg_t *c;

  (void)e;

  /* Not used by this implementation */
  *configchg = NULL;

  c = ares_malloc_zero(sizeof(*c));
  if (c == NULL) {
    return ARES_ENOMEM;
  }

  c->e          = e;
  c->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (c->inotify_fd == -1) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  /* We need to monitor /etc/resolv.conf, /etc/nsswitch.conf */
  if (inotify_add_watch(c->inotify_fd, "/etc",
                        IN_CREATE | IN_MODIFY | IN_MOVED_TO | IN_ONLYDIR) ==
      -1) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  status =
    ares_event_update(NULL, e, ARES_EVENT_FLAG_READ, ares_event_configchg_cb,
                      c->inotify_fd, c, ares_event_configchg_free, NULL);

done:
  if (status != ARES_SUCCESS) {
    ares_event_configchg_free(c);
  }
  return status;
}

#elif defined(_WIN32)

#  include <winsock2.h>
#  include <iphlpapi.h>
#  include <stdio.h>
#  include <windows.h>

struct ares_event_configchg {
  HANDLE               ifchg_hnd;
  ares_event_thread_t *e;
};

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
#  ifdef __WATCOMC__
  /* Not supported */
#  else
  if (configchg == NULL) {
    return;
  }

  if (configchg->ifchg_hnd != NULL) {
    CancelMibChangeNotify2(configchg->ifchg_hnd);
    configchg->ifchg_hnd = NULL;
  }

  ares_free(configchg);
#  endif
}

#  ifndef __WATCOMC__
static void ares_event_configchg_cb(PVOID                 CallerContext,
                                    PMIB_IPINTERFACE_ROW  Row,
                                    MIB_NOTIFICATION_TYPE NotificationType)
{
  ares_event_configchg_t *configchg = CallerContext;
  (void)Row;
  (void)NotificationType;
  ares_reinit(configchg->e->channel);
}
#  endif


ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
#  ifdef __WATCOMC__
  return ARES_ENOTIMP;
#  else
  ares_status_t status = ARES_SUCCESS;

  *configchg = ares_malloc_zero(sizeof(**configchg));
  if (*configchg == NULL) {
    return ARES_ENOMEM;
  }

  (*configchg)->e = e;

  /* NOTE: If a user goes into the control panel and changes the network
   *       adapter DNS addresses manually, this will NOT trigger a notification.
   *       We've also tried listening on NotifyUnicastIpAddressChange(), but
   *       that didn't get triggered either.
   */

  if (NotifyIpInterfaceChange(
        AF_UNSPEC, (PIPINTERFACE_CHANGE_CALLBACK)ares_event_configchg_cb,
        *configchg, FALSE, &(*configchg)->ifchg_hnd) != NO_ERROR) {
    status = ARES_ESERVFAIL;
    goto done;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_event_configchg_destroy(*configchg);
    *configchg = NULL;
  }

  return status;
#  endif
}

#elif defined(__APPLE__)

#  include <sys/types.h>
#  include <unistd.h>
#  include <notify.h>
#  include <dlfcn.h>

struct ares_event_configchg {
  int fd;
  int token;
};

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
  (void)configchg;

  /* Cleanup happens automatically */
}

static void ares_event_configchg_free(void *data)
{
  ares_event_configchg_t *configchg = data;
  if (configchg == NULL) {
    return;
  }

  if (configchg->fd >= 0) {
    notify_cancel(configchg->token);
    /* automatically closes fd */
    configchg->fd = -1;
  }

  ares_free(configchg);
}

static void ares_event_configchg_cb(ares_event_thread_t *e, ares_socket_t fd,
                                    void *data, ares_event_flags_t flags)
{
  ares_event_configchg_t *configchg = data;
  ares_bool_t             triggered = ARES_FALSE;

  (void)fd;
  (void)flags;

  while (1) {
    int     t = 0;
    ssize_t len;

    len = read(configchg->fd, &t, sizeof(t));

    if (len < (ssize_t)sizeof(t)) {
      break;
    }

    /* Token is read in network byte order (yeah, docs don't mention this) */
    t = (int)ntohl(t);

    if (t != configchg->token) {
      continue;
    }

    triggered = ARES_TRUE;
  }

  /* Only process after all events are read.  No need to process more often as
   * we don't want to reload the config back to back */
  if (triggered) {
    ares_reinit(e->channel);
  }
}

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
  ares_status_t status                               = ARES_SUCCESS;
  void         *handle                               = NULL;
  const char *(*pdns_configuration_notify_key)(void) = NULL;
  const char *notify_key                             = NULL;
  int         flags;

  *configchg = ares_malloc_zero(sizeof(**configchg));
  if (*configchg == NULL) {
    return ARES_ENOMEM;
  }

  /* Load symbol as it isn't normally public */
  handle = dlopen("/usr/lib/libSystem.dylib", RTLD_LAZY | RTLD_NOLOAD);
  if (handle == NULL) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  pdns_configuration_notify_key = dlsym(handle, "dns_configuration_notify_key");

  if (pdns_configuration_notify_key == NULL) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  notify_key = pdns_configuration_notify_key();
  if (notify_key == NULL) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  if (notify_register_file_descriptor(notify_key, &(*configchg)->fd, 0,
                                      &(*configchg)->token) !=
      NOTIFY_STATUS_OK) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  /* Set file descriptor to non-blocking */
  flags = fcntl((*configchg)->fd, F_GETFL, 0);
  fcntl((*configchg)->fd, F_SETFL, flags | O_NONBLOCK);

  /* Register file descriptor with event subsystem */
  status = ares_event_update(NULL, e, ARES_EVENT_FLAG_READ,
                             ares_event_configchg_cb, (*configchg)->fd,
                             *configchg, ares_event_configchg_free, NULL);

done:
  if (status != ARES_SUCCESS) {
    ares_event_configchg_free(*configchg);
    *configchg = NULL;
  }

  if (handle) {
    dlclose(handle);
  }

  return status;
}

#elif defined(HAVE_STAT)
#  ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif
#  ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#  endif

typedef struct {
  size_t size;
  time_t mtime;
} fileinfo_t;

struct ares_event_configchg {
  ares_bool_t           isup;
  ares__thread_t       *thread;
  ares__htable_strvp_t *filestat;
  ares__thread_mutex_t *lock;
  ares__thread_cond_t  *wake;
  const char           *resolvconf_path;
  ares_event_thread_t  *e;
};

static ares_status_t config_change_check(ares__htable_strvp_t *filestat,
                                         const char           *resolvconf_path)
{
  size_t      i;
  const char *configfiles[] = { resolvconf_path, "/etc/nsswitch.conf",
                                "/etc/netsvc.conf", "/etc/svc.conf", NULL };
  ares_bool_t changed       = ARES_FALSE;

  for (i = 0; configfiles[i] != NULL; i++) {
    fileinfo_t *fi = ares__htable_strvp_get_direct(filestat, configfiles[i]);
    struct stat st;

    if (stat(configfiles[i], &st) == 0) {
      if (fi == NULL) {
        fi = ares_malloc_zero(sizeof(*fi));
        if (fi == NULL) {
          return ARES_ENOMEM;
        }
        if (!ares__htable_strvp_insert(filestat, configfiles[i], fi)) {
          ares_free(fi);
          return ARES_ENOMEM;
        }
      }
      if (fi->size != (size_t)st.st_size || fi->mtime != (time_t)st.st_mtime) {
        changed = ARES_TRUE;
      }
      fi->size  = (size_t)st.st_size;
      fi->mtime = (time_t)st.st_mtime;
    } else if (fi != NULL) {
      /* File no longer exists, remove */
      ares__htable_strvp_remove(filestat, configfiles[i]);
      changed = ARES_TRUE;
    }
  }

  if (changed) {
    return ARES_SUCCESS;
  }
  return ARES_ENOTFOUND;
}

static void *ares_event_configchg_thread(void *arg)
{
  ares_event_configchg_t *c = arg;

  ares__thread_mutex_lock(c->lock);
  while (c->isup) {
    ares_status_t status;

    if (ares__thread_cond_timedwait(c->wake, c->lock, 30000) != ARES_ETIMEOUT) {
      continue;
    }

    /* make sure status didn't change even though we got a timeout */
    if (!c->isup) {
      break;
    }

    status = config_change_check(c->filestat, c->resolvconf_path);
    if (status == ARES_SUCCESS) {
      ares_reinit(c->e->channel);
    }
  }

  ares__thread_mutex_unlock(c->lock);
  return NULL;
}

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
  ares_status_t           status = ARES_SUCCESS;
  ares_event_configchg_t *c      = NULL;

  *configchg = NULL;

  c = ares_malloc_zero(sizeof(*c));
  if (c == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  c->e = e;

  c->filestat = ares__htable_strvp_create(ares_free);
  if (c->filestat == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  c->wake = ares__thread_cond_create();
  if (c->wake == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  c->resolvconf_path = c->e->channel->resolvconf_path;
  if (c->resolvconf_path == NULL) {
    c->resolvconf_path = PATH_RESOLV_CONF;
  }

  status = config_change_check(c->filestat, c->resolvconf_path);
  if (status == ARES_ENOMEM) {
    goto done;
  }

  c->isup = ARES_TRUE;
  status  = ares__thread_create(&c->thread, ares_event_configchg_thread, c);

done:
  if (status != ARES_SUCCESS) {
    ares_event_configchg_destroy(c);
  } else {
    *configchg = c;
  }
  return status;
}

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
  if (configchg == NULL) {
    return;
  }

  if (configchg->lock) {
    ares__thread_mutex_lock(configchg->lock);
  }

  configchg->isup = ARES_FALSE;
  if (configchg->wake) {
    ares__thread_cond_signal(configchg->wake);
  }

  if (configchg->lock) {
    ares__thread_mutex_unlock(configchg->lock);
  }

  if (configchg->thread) {
    void *rv = NULL;
    ares__thread_join(configchg->thread, &rv);
  }

  ares__thread_mutex_destroy(configchg->lock);
  ares__thread_cond_destroy(configchg->wake);
  ares__htable_strvp_destroy(configchg->filestat);
  ares_free(configchg);
}

#else

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e)
{
  /* No ability */
  return ARES_ENOTIMP;
}

void ares_event_configchg_destroy(ares_event_configchg_t *configchg)
{
  /* No-op */
}

#endif
