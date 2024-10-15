#include <stdint.h>
#include <string.h>
#include "mdadm.h"
#include "jbod.h"

static int mounted = 0;

// Simplified helper function to encode JBOD operations
static uint32_t encode_op(uint8_t cmd, uint8_t disk_id, uint8_t block_id) {
    return (cmd << 12) | (block_id << 4) | disk_id;
}

int mdadm_mount(void) {
    if (mounted) {
        return -1; // Already mounted
    }
    uint32_t op = encode_op(JBOD_MOUNT, 0, 0);
    if (jbod_operation(op, NULL) == 0) {
        mounted = 1;
        return 1; // Success
    }
    return -1; // Failure
}

int mdadm_unmount(void) {
    if (!mounted) {
        return -1; // Already unmounted
    }
    uint32_t op = encode_op(JBOD_UNMOUNT, 0, 0);
    if (jbod_operation(op, NULL) == 0) {
        mounted = 0;
        return 1; // Success
    }
    return -1; // Failure
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf) {
    if (!mounted) {
        return -3; // System is unmounted
    }
    if (read_len > 1024) {
        return -2; // Exceeds maximum read length
    }
    if (read_len > 0 && read_buf == NULL) {
        return -4; // Buffer is NULL
    }
    uint32_t max_addr = JBOD_NUM_DISKS * JBOD_DISK_SIZE;
    if (start_addr + read_len > max_addr || start_addr + read_len < start_addr) {
        return -1; // Invalid address range
    }

    uint32_t bytes_read = 0;

    while (bytes_read < read_len) {
        uint32_t addr = start_addr + bytes_read;
        uint32_t disk_num = addr / JBOD_DISK_SIZE;
        uint32_t addr_in_disk = addr % JBOD_DISK_SIZE;
        uint32_t block_num = addr_in_disk / JBOD_BLOCK_SIZE;
        uint32_t offset_in_block = addr_in_disk % JBOD_BLOCK_SIZE;

        uint32_t bytes_left_in_block = JBOD_BLOCK_SIZE - offset_in_block;
        uint32_t bytes_to_read = read_len - bytes_read;
        if (bytes_to_read > bytes_left_in_block) {
            bytes_to_read = bytes_left_in_block;
        }

        // Seek to the correct disk
        uint32_t op = encode_op(JBOD_SEEK_TO_DISK, disk_num & 0x0F, 0);
        if (jbod_operation(op, NULL) != 0) {
            return -4; // Disk seek failed
        }

        // Seek to the correct block
        op = encode_op(JBOD_SEEK_TO_BLOCK, 0, block_num & 0xFF);
        if (jbod_operation(op, NULL) != 0) {
            return -4; // Block seek failed
        }

        // Read the block
        uint8_t block[JBOD_BLOCK_SIZE];
        op = encode_op(JBOD_READ_BLOCK, 0, 0);
        if (jbod_operation(op, block) != 0) {
            return -4; // Block read failed
        }

        // Copy data to the buffer
        memcpy(read_buf + bytes_read, block + offset_in_block, bytes_to_read);

        bytes_read += bytes_to_read;
    }

    return bytes_read; // Total bytes read
}
