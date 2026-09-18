#ifndef FH8626_LIBGC1054_MIPI_REIMPLEMENTATION_H
#define FH8626_LIBGC1054_MIPI_REIMPLEMENTATION_H

#include <stdint.h>

int set_clk_rate(int type, uint32_t hz);

void SPISensor_Write(uint32_t reg, uint32_t value);
uint32_t SPISensor_Read(uint32_t reg);

void I2CSensor_WriteEx(uint32_t reg, uint32_t value, int quiet);
void I2CSensor_WriteEx_Multi(const uint16_t *regs, const uint16_t *values,
                             int quiet, int count);
void I2CSensor_Write_Multi(const uint16_t *regs, const uint16_t *values,
                           int count);
void I2CSensor_Write(uint32_t reg, uint32_t value);
uint32_t I2CSensor_Read(uint32_t reg);

uint32_t Sensor_Read(uint32_t reg);
void Sensor_Write(uint32_t reg, uint32_t value);
void Sensor_Write_Multi(const uint16_t *regs, const uint16_t *values,
                        int count);
void Sensor_WriteEx(uint32_t reg, uint32_t value, int quiet);

int SensorDevice_Init(uint16_t message_addr, uint32_t mode);
int SensorDevice_Close(void);
long SensorGetEnvInt(const char *name, long default_value);

/*
 * These stock exports return zero in r0 and have no internal callers. uintptr_t
 * preserves the target ARM ABI result without inventing an unproven semantic
 * pointer/integer type.
 */
uintptr_t GetDefaultParam(void);
uintptr_t GetContrast(void);
uintptr_t GetSaturation(void);
uintptr_t GetSharpness(void);
uint32_t *GetMirrorFlipBayerFormat(void);
uintptr_t GetSensorAwbGain(void);
uintptr_t GetSensorLtmCurve(void);

void *Sensor_Create(void);
void Sensor_Destory(void);

/* Divinus-owned strict adapters. The reconstructed stock exports above retain
 * original behavior; these adapters surface hardware failures and typed
 * controls to the native HAL without crossing the legacy callback ABI. */
int fh8626_gc1054_source_initialize_strict(void);
int fh8626_gc1054_source_close(void);
int fh8626_gc1054_source_set_format(uint32_t format);
int fh8626_gc1054_source_set_integration(uint32_t integration);
int fh8626_gc1054_source_set_gain(uint32_t gain);
int fh8626_gc1054_source_get_gain(uint32_t *gain);
int fh8626_gc1054_source_get_integration(uint32_t *integration);
int fh8626_gc1054_source_set_vts_multiplier(uint32_t multiplier);
int fh8626_gc1054_source_get_vi_attr(void *attr);
int fh8626_gc1054_source_set_mirror_flip(uint32_t logical);
int fh8626_gc1054_source_get_mirror_flip(uint32_t *logical);

#endif
