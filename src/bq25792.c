#include "bq25792.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct bq25792_dev {
  int fd;
  int bus;
  uint8_t addr;
  int inited;
};

/* Register map subset (TI BQ25792 datasheet) */
enum {
  REG0A_RECHG_CTRL      = 0x0A, /* CELL_1:0 in bits 7:6 (battery cell count) */
  REG10_CHG_CTRL_1      = 0x10, /* WATCHDOG_2:0 in bits 2:0, WD_RST in bit3 */
  REG14_CHG_CTRL_5      = 0x14, /* EN_IBAT in bit5 */

  REG1B_CHG_STATUS_0    = 0x1B, /* 8-bit */
  REG1C_CHG_STATUS_1    = 0x1C, /* 8-bit */
  REG26_FAULT_FLAG_0    = 0x26, /* 8-bit */
  REG27_FAULT_FLAG_1    = 0x27, /* 8-bit */

  REG2E_ADC_CONTROL     = 0x2E, /* 8-bit */

  /* ADC result registers are 16-bit */
  REG31_IBUS_ADC        = 0x31,
  REG33_IBAT_ADC        = 0x33,
  REG35_VBUS_ADC        = 0x35,
  REG3B_VBAT_ADC        = 0x3B,
  REG3D_VSYS_ADC        = 0x3D,
  REG41_TDIE_ADC        = 0x41,
};

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
static uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

/* Kaba Li-ion OCV -> SoC (per-cell, mV). Kendi kimyaniza/yuk profilinize gore kalibre edin. */
static int soc_from_vcell_mv(int vcell_mv) {
  static const int mv[]  = { 3300, 3400, 3500, 3600, 3650, 3700, 3800, 3900, 4000, 4100, 4200 };
  static const int soc[] = {    0,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100 };
  if (vcell_mv <= mv[0]) return 0;
  if (vcell_mv >= mv[10]) return 100;
  for (int i = 0; i < 10; i++) {
    if (vcell_mv >= mv[i] && vcell_mv <= mv[i+1]) {
      const int dv = mv[i+1] - mv[i];
      const int ds = soc[i+1] - soc[i];
      const int x  = vcell_mv - mv[i];
      return clampi(soc[i] + (ds * x) / dv, 0, 100);
    }
  }
  return 0;
}

int bq25792_open(bq25792_dev_t **out, int i2c_bus, uint8_t i2c_addr) {
  if (!out) return -EINVAL;
  *out = NULL;

  char devpath[32];
  snprintf(devpath, sizeof(devpath), "/dev/i2c-%d", i2c_bus);

  int fd = open(devpath, O_RDWR | O_CLOEXEC);
  if (fd < 0) return -errno;

  if (ioctl(fd, I2C_SLAVE, i2c_addr) < 0) {
    int e = -errno;
    close(fd);
    return e;
  }

  bq25792_dev_t *dev = (bq25792_dev_t*)calloc(1, sizeof(*dev));
  if (!dev) {
    close(fd);
    return -ENOMEM;
  }
  dev->fd = fd;
  dev->bus = i2c_bus;
  dev->addr = i2c_addr;
  dev->inited = 0;

  *out = dev;
  return 0;
}

void bq25792_close(bq25792_dev_t *dev) {
  if (!dev) return;
  if (dev->fd >= 0) close(dev->fd);
  free(dev);
}

int bq25792_read_u8(bq25792_dev_t *dev, uint8_t reg, uint8_t *val) {
  if (!dev || !val) return -EINVAL;
  int r = i2c_smbus_read_byte_data(dev->fd, reg);
  if (r < 0) return -errno;
  *val = (uint8_t)r;
  return 0;
}

int bq25792_read_u16(bq25792_dev_t *dev, uint8_t reg, uint16_t *val) {
  if (!dev || !val) return -EINVAL;
  int r = i2c_smbus_read_word_data(dev->fd, reg);
  if (r < 0) return -errno;

  /* SMBus word byte sirasi BQ25792 ile ters olabilir -> byte-swap */
  *val = swap16((uint16_t)r);
  return 0;
}

static int write_u8(bq25792_dev_t *dev, uint8_t reg, uint8_t v) {
  int r = i2c_smbus_write_byte_data(dev->fd, reg, v);
  if (r < 0) return -errno;
  return 0;
}

static int rmw_u8(bq25792_dev_t *dev, uint8_t reg, uint8_t clear_mask, uint8_t set_mask) {
  uint8_t v = 0;
  int rc = bq25792_read_u8(dev, reg, &v);
  if (rc) return rc;
  v = (uint8_t)((v & ~clear_mask) | set_mask);
  return write_u8(dev, reg, v);
}

static int bq25792_apply_safe_defaults(bq25792_dev_t *dev) {
  /* I2C watchdog'u disable et (REG10[2:0]=0) -> ADC_EN / EN_IBAT beklenmedik reset olmasin */
  int rc = rmw_u8(dev, REG10_CHG_CTRL_1, 0x07, 0x00);
  if (rc) return rc;

  /* WD status temizlemek icin WD_RST pulse (opsiyonel) */
  (void)rmw_u8(dev, REG10_CHG_CTRL_1, 0x00, (1u << 3));

  /* IBAT discharge current sensing enable (REG14[5]) */
  rc = rmw_u8(dev, REG14_CHG_CTRL_5, 0x00, (1u << 5));
  return rc;
}

/*
  REG2E (ADC Control):
   bit7 ADC_EN
   bit6 ADC_RATE: 0=continuous, 1=one-shot
   bit5-4 ADC_SAMPLE: 00=15-bit, 01=14-bit, 10=13-bit, 11=12-bit
*/
int bq25792_adc_enable(bq25792_dev_t *dev, bool enable_continuous, bool high_res_15bit) {
  if (!dev) return -EINVAL;

  uint8_t v = 0;
  v |= (1u << 7); /* ADC_EN */
  if (!enable_continuous) v |= (1u << 6); /* 1 = one-shot */
  /* ADC_SAMPLE[5:4]: 00=15-bit, 01=14-bit */
  if (!high_res_15bit) v |= (1u << 4);

  return write_u8(dev, REG2E_ADC_CONTROL, v);
}

const char* bq25792_chg_stat_str(uint8_t s) {
  switch (s & 0x7) {
    case 0: return "Not charging";
    case 1: return "Trickle charge";
    case 2: return "Pre-charge";
    case 3: return "Fast charge (CC)";
    case 4: return "Taper charge (CV)";
    case 6: return "Top-off timer active";
    case 7: return "Charge termination done";
    default: return "Reserved/unknown";
  }
}

const char* bq25792_vbus_stat_str(uint8_t s) {
  switch (s & 0xF) {
    case 0x0: return "No input / BHOT / BCOLD (OTG)";
    case 0x1: return "USB SDP (500mA)";
    case 0x2: return "USB CDP (1.5A)";
    case 0x3: return "USB DCP (3.25A)";
    case 0x4: return "HVDCP (1.5A)";
    case 0x5: return "Unknown adaptor (3A)";
    case 0x6: return "Non-standard adaptor (1A/2A/2.1A/2.4A)";
    case 0x7: return "OTG mode";
    case 0x8: return "Not qualified adaptor";
    case 0xB: return "Device directly powered from VBUS";
    default:  return "Reserved/unknown";
  }
}

int bq25792_read_status(bq25792_dev_t *dev, bq25792_status_t *st, bool ensure_adc_on) {
  if (!dev || !st) return -EINVAL;
  memset(st, 0, sizeof(*st));

  if (!dev->inited) {
    (void)bq25792_apply_safe_defaults(dev);
    dev->inited = 1;
  }

  /* Cell count from REG0A[7:6] (1s..4s) */
  uint8_t reg0a = 0;
  if (bq25792_read_u8(dev, REG0A_RECHG_CTRL, &reg0a) == 0) {
    uint8_t cell = (reg0a >> 6) & 0x3;
    st->cell_count = (uint8_t)(cell + 1);
  } else {
    st->cell_count = 1;
  }

  uint8_t s0 = 0, s1 = 0;
  int rc = bq25792_read_u8(dev, REG1B_CHG_STATUS_0, &s0);
  if (rc) return rc;
  rc = bq25792_read_u8(dev, REG1C_CHG_STATUS_1, &s1);
  if (rc) return rc;

  st->iindpm = (s0 >> 7) & 1;
  st->vindpm = (s0 >> 6) & 1;
  st->watchdog_expired = (s0 >> 5) & 1;
  st->poor_source = (s0 >> 4) & 1;
  st->pg = (s0 >> 3) & 1;
  st->ac2_present = (s0 >> 2) & 1;
  st->ac1_present = (s0 >> 1) & 1;
  st->vbus_present = (s0 >> 0) & 1;

  st->chg_stat = (s1 >> 5) & 0x7;
  st->vbus_stat = (s1 >> 1) & 0xF;
  st->bc12_done = (s1 >> 0) & 1;

  /* Fault flags */
  uint8_t f0 = 0, f1 = 0;
  (void)bq25792_read_u8(dev, REG26_FAULT_FLAG_0, &f0);
  (void)bq25792_read_u8(dev, REG27_FAULT_FLAG_1, &f1);
  st->fault0 = f0;
  st->fault1 = f1;
  st->fault_any = (f0 != 0) || (f1 != 0) || st->watchdog_expired || st->poor_source;

  /* ADC enable if requested */
  if (ensure_adc_on) {
    (void)bq25792_adc_enable(dev, true, true);
    /* ADC enable sonrasi ilk conversion 0 gelebilir */
    usleep(50000);
  }

  /* ADC reads (LSB=1mV/1mA, TDIE=0.5C) */
  uint16_t w = 0;
  if (bq25792_read_u16(dev, REG31_IBUS_ADC, &w) == 0) st->ibus_ma = (int16_t)w;
  if (bq25792_read_u16(dev, REG33_IBAT_ADC, &w) == 0) st->ibat_ma = (int16_t)w;
  if (bq25792_read_u16(dev, REG35_VBUS_ADC, &w) == 0) st->vbus_mv = (int)w;
  if (bq25792_read_u16(dev, REG3B_VBAT_ADC, &w) == 0) st->vbat_mv = (int)w;
  if (bq25792_read_u16(dev, REG3D_VSYS_ADC, &w) == 0) st->vsys_mv = (int)w;
  if (bq25792_read_u16(dev, REG41_TDIE_ADC, &w) == 0) st->tdie_c = (float)((int16_t)w) * 0.5f;

  /* SoC estimate from per-cell voltage */
  if (st->cell_count < 1) st->cell_count = 1;
  int vcell = (st->vbat_mv > 0) ? (st->vbat_mv / (int)st->cell_count) : 0;
  st->soc_pct_est = soc_from_vcell_mv(vcell);

  return 0;
}
