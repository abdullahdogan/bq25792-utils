#include "bq25792.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int env_int(const char *name, int defv) {
  const char *s = getenv(name);
  if (!s || !*s) return defv;
  return (int)strtol(s, NULL, 0);
}

static void print_usage(const char *argv0) {
  fprintf(stderr,
    "Usage:\n"
    "  %s [--bus N] [--addr 0x6b] [--no-adc] status [--json]\n"
    "  %s [--bus N] [--addr 0x6b] raw\n\n"
    "Environment defaults:\n"
    "  BQ_I2C_BUS   (e.g., 10)\n"
    "  BQ_I2C_ADDR  (e.g., 0x6b)\n",
    argv0, argv0);
}

static void json_bool(const char *k, int v, int *first) {
  printf("%s\"%s\":%s", (*first ? "" : ","), k, v ? "true" : "false");
  *first = 0;
}

static void json_int(const char *k, int v, int *first) {
  printf("%s\"%s\":%d", (*first ? "" : ","), k, v);
  *first = 0;
}

static void json_str(const char *k, const char *v, int *first) {
  printf("%s\"%s\":\"", (*first ? "" : ","), k);
  for (const char *p = v; *p; p++) {
    if (*p == '"' || *p == '\\') putchar('\\');
    putchar(*p);
  }
  printf("\"");
  *first = 0;
}

int main(int argc, char **argv) {
  int bus  = env_int("BQ_I2C_BUS", 10);
  int addr = env_int("BQ_I2C_ADDR", 0x6B);
  int ensure_adc = 1;

  static struct option long_opts[] = {
    {"bus", required_argument, 0, 'b'},
    {"addr", required_argument, 0, 'a'},
    {"no-adc", no_argument, 0, 'n'},
    {"help", no_argument, 0, 'h'},
    {0,0,0,0}
  };

  int c;
  while ((c = getopt_long(argc, argv, "b:a:nh", long_opts, NULL)) != -1) {
    switch (c) {
      case 'b': bus = (int)strtol(optarg, NULL, 0); break;
      case 'a': addr = (int)strtol(optarg, NULL, 0); break;
      case 'n': ensure_adc = 0; break;
      case 'h': default: print_usage(argv[0]); return 2;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return 2;
  }

  const char *cmd = argv[optind++];

  bq25792_dev_t *dev = NULL;
  int rc = bq25792_open(&dev, bus, (uint8_t)addr);
  if (rc) {
    fprintf(stderr, "bqctl: open failed (bus=%d addr=0x%02x): %s\n", bus, addr & 0xFF, strerror(-rc));
    return 1;
  }

  if (strcmp(cmd, "status") == 0) {
    int json = 0;
    if (optind < argc && strcmp(argv[optind], "--json") == 0) json = 1;

    bq25792_status_t st;
    rc = bq25792_read_status(dev, &st, ensure_adc);
    if (rc) {
      fprintf(stderr, "bqctl: read_status failed: %s\n", strerror(-rc));
      bq25792_close(dev);
      return 1;
    }

    if (json) {
      int first = 1;
      printf("{");
      json_bool("vbus_present", st.vbus_present, &first);
      json_bool("ac1_present", st.ac1_present, &first);
      json_bool("ac2_present", st.ac2_present, &first);
      json_bool("pg", st.pg, &first);
      json_bool("iindpm", st.iindpm, &first);
      json_bool("vindpm", st.vindpm, &first);
      json_bool("watchdog_expired", st.watchdog_expired, &first);
      json_bool("poor_source", st.poor_source, &first);

      json_int("cell_count", st.cell_count, &first);
      json_int("chg_stat", st.chg_stat, &first);
      json_str("chg_stat_str", bq25792_chg_stat_str(st.chg_stat), &first);

      json_int("vbus_stat", st.vbus_stat, &first);
      json_str("vbus_stat_str", bq25792_vbus_stat_str(st.vbus_stat), &first);
      json_bool("bc12_done", st.bc12_done, &first);

      json_int("ibus_ma", st.ibus_ma, &first);
      json_int("ibat_ma", st.ibat_ma, &first);
      json_int("vbus_mv", st.vbus_mv, &first);
      json_int("vbat_mv", st.vbat_mv, &first);
      json_int("vsys_mv", st.vsys_mv, &first);

      /* float */
      printf("%s\"tdie_c\":%.1f", (first ? "" : ","), st.tdie_c); first = 0;

      json_int("soc_pct_est", st.soc_pct_est, &first);

      json_int("fault0", st.fault0, &first);
      json_int("fault1", st.fault1, &first);
      json_bool("fault_any", st.fault_any, &first);

      printf("}\n");
    } else {
      printf("BQ25792 status (bus=%d addr=0x%02x)\n", bus, addr & 0xFF);
      printf("  Input : VBUS=%d AC1=%d AC2=%d PG=%d\n", st.vbus_present, st.ac1_present, st.ac2_present, st.pg);
      printf("  DPM   : IINDPM=%d VINDPM=%d poor_src=%d wd_exp=%d\n", st.iindpm, st.vindpm, st.poor_source, st.watchdog_expired);
      printf("  Charge: chg_stat=%u (%s)\n", st.chg_stat, bq25792_chg_stat_str(st.chg_stat));
      printf("         vbus_stat=0x%X (%s) bc12_done=%d\n", st.vbus_stat, bq25792_vbus_stat_str(st.vbus_stat), st.bc12_done);
      printf("  ADC   : VBUS=%dmV VBAT=%dmV VSYS=%dmV IBUS=%dmA IBAT=%dmA TDIE=%.1fC\n",
             st.vbus_mv, st.vbat_mv, st.vsys_mv, st.ibus_ma, st.ibat_ma, st.tdie_c);
      printf("  Batt  : cells=%u SoC_est=%d%%\n", st.cell_count, st.soc_pct_est);
      printf("  Fault : any=%d fault0=0x%02X fault1=0x%02X\n", st.fault_any, st.fault0, st.fault1);
    }
  } else if (strcmp(cmd, "raw") == 0) {
    uint8_t v8;
    uint16_t v16;

    if (bq25792_read_u8(dev, 0x0A, &v8) == 0) printf("REG0A: 0x%02X\n", v8);
    if (bq25792_read_u8(dev, 0x1B, &v8) == 0) printf("REG1B: 0x%02X\n", v8);
    if (bq25792_read_u8(dev, 0x1C, &v8) == 0) printf("REG1C: 0x%02X\n", v8);
    if (bq25792_read_u8(dev, 0x26, &v8) == 0) printf("REG26: 0x%02X\n", v8);
    if (bq25792_read_u8(dev, 0x27, &v8) == 0) printf("REG27: 0x%02X\n", v8);

    if (bq25792_read_u16(dev, 0x31, &v16) == 0) printf("REG31 (IBUS): 0x%04X\n", v16);
    if (bq25792_read_u16(dev, 0x33, &v16) == 0) printf("REG33 (IBAT): 0x%04X\n", v16);
    if (bq25792_read_u16(dev, 0x35, &v16) == 0) printf("REG35 (VBUS): 0x%04X\n", v16);
    if (bq25792_read_u16(dev, 0x3B, &v16) == 0) printf("REG3B (VBAT): 0x%04X\n", v16);
    if (bq25792_read_u16(dev, 0x3D, &v16) == 0) printf("REG3D (VSYS): 0x%04X\n", v16);
    if (bq25792_read_u16(dev, 0x41, &v16) == 0) printf("REG41 (TDIE): 0x%04X\n", v16);
  } else {
    print_usage(argv[0]);
    bq25792_close(dev);
    return 2;
  }

  bq25792_close(dev);
  return 0;
}
