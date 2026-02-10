#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bq25792_dev bq25792_dev_t;

/* Cihazin parse edilmis durum anlik goruntusu */
typedef struct {
  /* Giris / adaptor */
  bool vbus_present;
  bool ac1_present;
  bool ac2_present;
  bool pg;
  bool iindpm;
  bool vindpm;
  bool watchdog_expired;
  bool poor_source;

  /* Sarj durumu */
  uint8_t chg_stat;   /* 0..7 */
  uint8_t vbus_stat;  /* 0..15 */
  bool bc12_done;

  /* Fault flag'lar */
  uint8_t fault0; /* REG26 */
  uint8_t fault1; /* REG27 */
  bool fault_any;

  /* ADC olcumleri (ADC_EN acikken) */
  int ibus_ma;   /* signed */
  int ibat_ma;   /* signed (pozitif = sarj, negatif = desarj) */
  int vbus_mv;
  int vbat_mv;   /* pack voltage */
  int vsys_mv;
  float tdie_c;

  /* Pil konfig / tahmin */
  uint8_t cell_count;   /* 1..4 */
  int soc_pct_est;      /* 0..100, VBAT/cell uzerinden kaba tahmin */
} bq25792_status_t;

/* Open/close */
int  bq25792_open(bq25792_dev_t **dev, int i2c_bus, uint8_t i2c_addr);
void bq25792_close(bq25792_dev_t *dev);

/* Register okuma */
int bq25792_read_u8 (bq25792_dev_t *dev, uint8_t reg, uint8_t *val);
int bq25792_read_u16(bq25792_dev_t *dev, uint8_t reg, uint16_t *val);

/* ADC control (REG2E) */
int bq25792_adc_enable(bq25792_dev_t *dev, bool enable_continuous, bool high_res_15bit);

/* Durum snapshot */
int bq25792_read_status(bq25792_dev_t *dev, bq25792_status_t *st, bool ensure_adc_on);

/* String helper'lar */
const char* bq25792_chg_stat_str(uint8_t chg_stat);
const char* bq25792_vbus_stat_str(uint8_t vbus_stat);

#ifdef __cplusplus
}
#endif
