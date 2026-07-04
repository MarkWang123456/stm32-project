#include "system_packet.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static uint32_t SystemPacket_CalcChecksum32(const SystemDataPacketV0 *pkt)
{
    const uint8_t *bytes = (const uint8_t *)pkt;
    uint32_t sum = 0;

    for (uint32_t i = 0; i < offsetof(SystemDataPacketV0, checksum32); i++)
    {
        sum += bytes[i];
    }

    return sum;
}


static void SystemPacket_PrintHexDump(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        printf("%02X ", (unsigned int)data[i]);

        if ((i + 1) % 16 == 0)
        {
            printf("\r\n");
        }
    }

    if (len % 16 != 0)
    {
        printf("\r\n");
    }
}


static void SystemPacket_BuildGoldenPacket(SystemDataPacketV0 *pkt)
{
    memset(pkt, 0, sizeof(SystemDataPacketV0));

    pkt->magic = SYSTEM_PACKET_MAGIC;
    pkt->version = SYSTEM_PACKET_VERSION;
    pkt->header_size = SYSTEM_PACKET_HEADER_SIZE;
    pkt->packet_size = SYSTEM_PACKET_SIZE;

    pkt->sequence = 1;
    pkt->timestamp_ms = 123456;

    pkt->accel_x_raw = 100;
    pkt->accel_y_raw = -100;
    pkt->accel_z_raw = 16384;

    pkt->gyro_x_raw = 200;
    pkt->gyro_y_raw = -200;
    pkt->gyro_z_raw = 0;

    pkt->temperature_c_x100 = 2534;
    pkt->pressure_pa = 101325;
    pkt->humidity_percent_x100 = 6120;

    pkt->sensor_status = 0x0003;
    pkt->system_flags = 0x0004;

    pkt->imu_late_count = 0;
    pkt->i2c_error_count = 2;
    pkt->i2c_recovery_count = 1;

    pkt->checksum32 = SystemPacket_CalcChecksum32(pkt);
}


void SystemPacket_GoldenPacketTest(void)
{
    SystemDataPacketV0 pkt;

    SystemPacket_BuildGoldenPacket(&pkt);

    printf("\r\n[SystemPacket Golden Test]\r\n");
    printf("sizeof(SystemDataPacketV0) = %lu\r\n", (unsigned long)sizeof(SystemDataPacketV0));
    printf("magic = 0x%08lX\r\n", (unsigned long)pkt.magic);
    printf("version = %u\r\n", pkt.version);
    printf("header_size = %u\r\n", pkt.header_size);
    printf("packet_size = %u\r\n", pkt.packet_size);
    printf("sequence = %lu\r\n", (unsigned long)pkt.sequence);
    printf("timestamp_ms = %lu\r\n", (unsigned long)pkt.timestamp_ms);
    printf("temperature_c_x100 = %ld\r\n", (long)pkt.temperature_c_x100);
    printf("pressure_pa = %lu\r\n", (unsigned long)pkt.pressure_pa);
    printf("humidity_percent_x100 = %lu\r\n", (unsigned long)pkt.humidity_percent_x100);
    printf("checksum32 = 0x%08lX\r\n", (unsigned long)pkt.checksum32);

    printf("Hex dump:\r\n");
    SystemPacket_PrintHexDump((const uint8_t *)&pkt, sizeof(pkt));
}