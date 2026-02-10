#include "bq25792.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int sig) {
  (void)sig;
  g_stop = 1;
}

static int env_int(const char *name, int defv) {
  const char *s = getenv(name);
  if (!s || !*s) return defv;
  return (int)strtol(s, NULL, 0);
}

static const char* env_str(const char *name, const char *defv) {
  const char *s = getenv(name);
  return (s && *s) ? s : defv;
}

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int mkdir_p_for_file(const char *path) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  char *slash = strrchr(tmp, '/');
  if (!slash || slash == tmp) return 0;
  *slash = '\0';
  if (mkdir(tmp, 0755) == 0) return 0;
  if (errno == EEXIST) return 0;
  return -errno;
}

static int atomic_write(const char *path, const char *data, size_t len) {
  (void)mkdir_p_for_file(path);

  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  FILE *f = fopen(tmp, "wb");
  if (!f) return -errno;

  if (fwrite(data, 1, len, f) != len) {
    int e = -errno;
    fclose(f);
    unlink(tmp);
    return e;
  }

  fflush(f);
  int fd = fileno(f);
  if (fd >= 0) fsync(fd);
  fclose(f);

  if (rename(tmp, path) != 0) {
    int e = -errno;
    unlink(tmp);
    return e;
  }
  return 0;
}

typedef struct {
  int soc_display;            /* 0..100, third-party */
  float soc_filt;             /* filter state */
  long long last_change_ms;   /* rate limiting */
  int stable_cnt;             /* consecutive stability */
  int last_dir;               /* -1,0,+1 */
} soc_filter_t;

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static void soc_filter_init(soc_filter_t *f, int soc0) {
  memset(f, 0, sizeof(*f));
  f->soc_display = clampi(soc0, 0, 100);
  f->soc_filt = (float)f->soc_display;
  f->last_change_ms = now_ms();
  f->stable_cnt = 0;
  f->last_dir = 0;
}

static int infer_dir(const bq25792_status_t *st) {
  if (st->ibat_ma > 50) return +1;
  if (st->ibat_ma < -50) return -1;
  return 0;
}

/* Stabilization:
   - EMA (alpha=0.15)
   - direction-based anti-jitter
   - rate limit: max 1%/minute
*/
static void soc_filter_update(soc_filter_t *f, int soc_raw, int dir) {
  soc_raw = clampi(soc_raw, 0, 100);

  const float alpha = 0.15f;
  f->soc_filt = f->soc_filt + alpha * ((float)soc_raw - f->soc_filt);

  int target = (int)(f->soc_filt + 0.5f);
  target = clampi(target, 0, 100);

  if (dir != f->last_dir) {
    f->stable_cnt = 0;
    f->last_dir = dir;
  }

  int disp = f->soc_display;
  int diff = target - disp;
  if (diff == 0) {
    f->stable_cnt = 0;
    return;
  }

  long long t = now_ms();
  const long long min_step_ms = 60 * 1000LL;
  const int big_jump = 5;

  if (dir > 0) { /* charging */
    if (diff > 0) {
      if ((t - f->last_change_ms) >= min_step_ms) {
        f->soc_display += 1;
        f->last_change_ms = t;
      }
    } else { /* diff < 0 */
      if ((-diff) >= big_jump) {
        f->stable_cnt++;
        if (f->stable_cnt >= 6 && (t - f->last_change_ms) >= min_step_ms) {
          f->soc_display -= 1;
          f->last_change_ms = t;
          f->stable_cnt = 0;
        }
      } else {
        f->stable_cnt = 0;
      }
    }
  } else if (dir < 0) { /* discharging */
    if (diff < 0) {
      if ((t - f->last_change_ms) >= min_step_ms) {
        f->soc_display -= 1;
        f->last_change_ms = t;
      }
    } else { /* diff > 0 */
      if (diff >= big_jump) {
        f->stable_cnt++;
        if (f->stable_cnt >= 60 && (t - f->last_change_ms) >= (10 * min_step_ms)) {
          f->soc_display += 1;
          f->last_change_ms = t;
          f->stable_cnt = 0;
        }
      } else {
        f->stable_cnt = 0;
      }
    }
  } else { /* idle */
    if (abs(diff) >= 2) {
      f->stable_cnt++;
      if (f->stable_cnt >= 6 && (t - f->last_change_ms) >= min_step_ms) {
        f->soc_display += (diff > 0) ? 1 : -1;
        f->last_change_ms = t;
        f->stable_cnt = 0;
      }
    } else {
      f->stable_cnt = 0;
    }
  }

  f->soc_display = clampi(f->soc_display, 0, 100);
}

static void sleep_ms(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

int main(void) {
  signal(SIGINT, on_sig);
  signal(SIGTERM, on_sig);

  const int bus = env_int("BQ_I2C_BUS", 10);
  const int addr = env_int("BQ_I2C_ADDR", 0x6B);
  const int interval_sec = env_int("BQ_INTERVAL_SEC", 10);
  const char *out_path = env_str("BQ_STATUS_PATH", "/run/bq25792/status.json");

  bq25792_dev_t *dev = NULL;
  int rc = bq25792_open(&dev, bus, (uint8_t)addr);
  if (rc) {
    fprintf(stderr, "bq25792d: open failed (bus=%d addr=0x%02x): %s\n", bus, addr & 0xFF, strerror(-rc));
    return 1;
  }

  soc_filter_t filt;
  int filt_inited = 0;

  while (!g_stop) {
    bq25792_status_t st;
    rc = bq25792_read_status(dev, &st, true);
    if (rc) {
      fprintf(stderr, "bq25792d: read_status failed: %s\n", strerror(-rc));
      sleep_ms(interval_sec * 1000);
      continue;
    }

    int dir = infer_dir(&st);
    if (!filt_inited) {
      soc_filter_init(&filt, st.soc_pct_est);
      filt_inited = 1;
    } else {
      soc_filter_update(&filt, st.soc_pct_est, dir);
    }

    char json[1024];
    long long tms = now_ms();
    int n = snprintf(json, sizeof(json),
      "{"
        "\"ts_ms\":%lld,"
        "\"bus\":%d,"
        "\"addr\":\"0x%02x\","
        "\"vbus_present\":%s,"
        "\"pg\":%s,"
        "\"chg_stat\":%u,"
        "\"chg_stat_str\":\"%s\","
        "\"vbus_stat\":%u,"
        "\"vbus_stat_str\":\"%s\","
        "\"fault_any\":%s,"
        "\"fault0\":%u,"
        "\"fault1\":%u,"
        "\"vbat_mv\":%d,"
        "\"vsys_mv\":%d,"
        "\"vbus_mv\":%d,"
        "\"ibat_ma\":%d,"
        "\"ibus_ma\":%d,"
        "\"tdie_c\":%.1f,"
        "\"cell_count\":%u,"
        "\"soc_pct\":%d,"
        "\"soc_raw\":%d,"
        "\"soc_filt\":%.2f"
      "}\n",
      tms,
      bus,
      addr & 0xFF,
      st.vbus_present ? "true" : "false",
      st.pg ? "true" : "false",
      st.chg_stat,
      bq25792_chg_stat_str(st.chg_stat),
      st.vbus_stat,
      bq25792_vbus_stat_str(st.vbus_stat),
      st.fault_any ? "true" : "false",
      (unsigned)st.fault0,
      (unsigned)st.fault1,
      st.vbat_mv,
      st.vsys_mv,
      st.vbus_mv,
      st.ibat_ma,
      st.ibus_ma,
      st.tdie_c,
      (unsigned)st.cell_count,
      filt.soc_display,
      st.soc_pct_est,
      (double)filt.soc_filt
    );

    if (n > 0 && (size_t)n < sizeof(json)) {
      (void)atomic_write(out_path, json, (size_t)n);
    }

    sleep_ms(interval_sec * 1000);
  }

  bq25792_close(dev);
  return 0;
}
