#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SYSTEM_PACKET_VERSION      0u
#define SYSTEM_PACKET_HEADER_SIZE  16u
#define SYSTEM_PACKET_SIZE         60u
#define SYSTEM_PACKET_MAGIC        0x30504453u

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  header_size;
    uint16_t packet_size;

    uint32_t sequence;
    uint32_t timestamp_ms;

    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;

    int32_t  temperature_c_x100;
    uint32_t pressure_pa;
    uint32_t humidity_percent_x100;

    uint16_t sensor_status;
    uint16_t system_flags;

    uint32_t imu_late_count;
    uint32_t i2c_error_count;
    uint32_t i2c_recovery_count;

    uint32_t checksum32;
} SystemDataPacketV0;

_Static_assert(sizeof(SystemDataPacketV0) == SYSTEM_PACKET_SIZE,
               "SystemDataPacketV0 size must be 60 bytes");

static uint32_t calc_checksum32(const SystemDataPacketV0 *pkt)
{
    const uint8_t *bytes = (const uint8_t *)pkt;
    uint32_t sum = 0;

    for (uint32_t i = 0; i < offsetof(SystemDataPacketV0, checksum32); i++)
    {
        sum += bytes[i];
    }

    return sum;
}

int main(void)
{
    const uint8_t golden_packet[SYSTEM_PACKET_SIZE] = {
        0x53, 0x44, 0x50, 0x30, 0x00, 0x10, 0x3C, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x40, 0xE2, 0x01, 0x00,
        0x64, 0x00, 0x9C, 0xFF, 0x00, 0x40, 0xC8, 0x00,
        0x38, 0xFF, 0x00, 0x00, 0xE6, 0x09, 0x00, 0x00,
        0xCD, 0x8B, 0x01, 0x00, 0xE8, 0x17, 0x00, 0x00,
        0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x16, 0x0A, 0x00, 0x00
    };

    SystemDataPacketV0 pkt;

    memcpy(&pkt, golden_packet, sizeof(pkt));

    uint32_t calculated_checksum = calc_checksum32(&pkt);

    printf("[Raspberry Pi C Parser Test]\n");
    printf("sizeof(SystemDataPacketV0) = %zu\n", sizeof(SystemDataPacketV0));

    printf("magic = 0x%08X\n", pkt.magic);
    printf("version = %u\n", pkt.version);
    printf("header_size = %u\n", pkt.header_size);
    printf("packet_size = %u\n", pkt.packet_size);
    printf("sequence = %u\n", pkt.sequence);
    printf("timestamp_ms = %u\n", pkt.timestamp_ms);

    printf("accel_x_raw = %d\n", pkt.accel_x_raw);
    printf("accel_y_raw = %d\n", pkt.accel_y_raw);
    printf("accel_z_raw = %d\n", pkt.accel_z_raw);

    printf("gyro_x_raw = %d\n", pkt.gyro_x_raw);
    printf("gyro_y_raw = %d\n", pkt.gyro_y_raw);
    printf("gyro_z_raw = %d\n", pkt.gyro_z_raw);

    printf("temperature_c_x100 = %d\n", pkt.temperature_c_x100);
    printf("temperature_c = %.2f C\n", pkt.temperature_c_x100 / 100.0);

    printf("pressure_pa = %u\n", pkt.pressure_pa);

    printf("humidity_percent_x100 = %u\n", pkt.humidity_percent_x100);
    printf("humidity_percent = %.2f %%\n", pkt.humidity_percent_x100 / 100.0);

    printf("sensor_status = 0x%04X\n", pkt.sensor_status);
    printf("system_flags = 0x%04X\n", pkt.system_flags);

    printf("imu_late_count = %u\n", pkt.imu_late_count);
    printf("i2c_error_count = %u\n", pkt.i2c_error_count);
    printf("i2c_recovery_count = %u\n", pkt.i2c_recovery_count);

    printf("packet checksum32 = 0x%08X\n", pkt.checksum32);
    printf("calculated checksum32 = 0x%08X\n", calculated_checksum);

    if (pkt.magic != SYSTEM_PACKET_MAGIC)
    {
        printf("[FAIL] magic mismatch\n");
        return 1;
    }

    if (pkt.version != SYSTEM_PACKET_VERSION)
    {
        printf("[FAIL] version mismatch\n");
        return 1;
    }

    if (pkt.header_size != SYSTEM_PACKET_HEADER_SIZE)
    {
        printf("[FAIL] header_size mismatch\n");
        return 1;
    }

    if (pkt.packet_size != SYSTEM_PACKET_SIZE)
    {
        printf("[FAIL] packet_size mismatch\n");
        return 1;
    }

    if (pkt.checksum32 != calculated_checksum)
    {
        printf("[FAIL] checksum mismatch\n");
        return 1;
    }

    printf("[PASS] golden packet parsed successfully\n");

    return 0;
}