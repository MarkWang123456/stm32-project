#ifndef SYSTEM_PACKET_H
#define SYSTEM_PACKET_H

#include <stdint.h>
#include <stddef.h>

#define SYSTEM_PACKET_VERSION      0u
#define SYSTEM_PACKET_HEADER_SIZE  16u
#define SYSTEM_PACKET_SIZE         60u

/*
 * Magic: "SDP0"
 * Little-endian memory order:
 * 'S' = 0x53
 * 'D' = 0x44
 * 'P' = 0x50
 * '0' = 0x30
 */
#define SYSTEM_PACKET_MAGIC  0x30504453u

typedef struct __attribute__((packed))
{
    uint32_t magic;                  // offset 0
    uint8_t  version;                // offset 4
    uint8_t  header_size;            // offset 5
    uint16_t packet_size;            // offset 6

    uint32_t sequence;               // offset 8
    uint32_t timestamp_ms;           // offset 12

    int16_t accel_x_raw;             // offset 16
    int16_t accel_y_raw;             // offset 18
    int16_t accel_z_raw;             // offset 20
    int16_t gyro_x_raw;              // offset 22
    int16_t gyro_y_raw;              // offset 24
    int16_t gyro_z_raw;              // offset 26

    int32_t  temperature_c_x100;     // offset 28
    uint32_t pressure_pa;            // offset 32
    uint32_t humidity_percent_x100;  // offset 36

    uint16_t sensor_status;          // offset 40
    uint16_t system_flags;           // offset 42

    uint32_t imu_late_count;         // offset 44
    uint32_t i2c_error_count;        // offset 48
    uint32_t i2c_recovery_count;     // offset 52

    uint32_t checksum32;             // offset 56
} SystemDataPacketV0;

_Static_assert(sizeof(SystemDataPacketV0) == SYSTEM_PACKET_SIZE,
               "SystemDataPacketV0 size must be 60 bytes");

_Static_assert(offsetof(SystemDataPacketV0, magic) == 0,
               "magic offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, version) == 4,
               "version offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, header_size) == 5,
               "header_size offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, packet_size) == 6,
               "packet_size offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, sequence) == 8,
               "sequence offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, timestamp_ms) == 12,
               "timestamp_ms offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, accel_x_raw) == 16,
               "accel_x_raw offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, temperature_c_x100) == 28,
               "temperature_c_x100 offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, sensor_status) == 40,
               "sensor_status offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, imu_late_count) == 44,
               "imu_late_count offset mismatch");

_Static_assert(offsetof(SystemDataPacketV0, checksum32) == 56,
               "checksum32 offset mismatch");

void SystemPacket_GoldenPacketTest(void);

#endif


