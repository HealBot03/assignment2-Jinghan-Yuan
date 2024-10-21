#include <stdint.h>
#include <string.h>
#include "mdadm.h"
#include "jbod.h"

static int mounted = 0;

// helper function to encode JBOD operations into a single uint32_t.
// combines the command, disk ID, and block ID.
static uint32_t encode_op(uint8_t cmd, uint8_t disk_id, uint8_t block_id) {
    return (cmd << 12) | (block_id << 4) | disk_id;
}

int mdadm_mount(void) {
    // check if already mounted
    if (mounted) {
        return -1; // do not mount if already mounted
    }
    // mount operation
    uint32_t op = encode_op(JBOD_MOUNT, 0, 0);
    // attempt to mount
    if (jbod_operation(op, NULL) == 0) {
        mounted = 1;
        return 1; // mount successful
    }
    return -1; // mount failed
}

int mdadm_unmount(void) {
    // check mounting status
    if (!mounted) {
        return -1; // can't unmount if not mounted
    }
    // unmount operation
    uint32_t op = encode_op(JBOD_UNMOUNT, 0, 0);
    // attempt to unmount
    if (jbod_operation(op, NULL) == 0) {
        mounted = 0;
        return 1; // unmount successful
    }
    return -1; // unmount failed
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf) {
    // mount check
    if (!mounted) {
        return -3; 
    }
    // read_len within maximum check
    if (read_len > 1024) {
        return -2; 
    }
    // check buffer is not NULL
    if (read_len > 0 && read_buf == NULL) {
        return -4; 
    }
    // check valid address range
    uint32_t max_addr = JBOD_NUM_DISKS * JBOD_DISK_SIZE;
    if (start_addr + read_len > max_addr || start_addr + read_len < start_addr) {
        return -1;
    }

    uint32_t bytes_read = 0;

    // loop until all the bytes are read
    while (bytes_read < read_len) {
        uint32_t addr = start_addr + bytes_read;
        uint32_t disk_num = addr / JBOD_DISK_SIZE;
        uint32_t addr_in_disk = addr % JBOD_DISK_SIZE;
        uint32_t block_num = addr_in_disk / JBOD_BLOCK_SIZE;
        uint32_t offset_in_block = addr_in_disk % JBOD_BLOCK_SIZE;

        // how many bytes can be read from the current block
        uint32_t bytes_left_in_block = JBOD_BLOCK_SIZE - offset_in_block;
        uint32_t bytes_to_read = read_len - bytes_read;
        if (bytes_to_read > bytes_left_in_block) {
            bytes_to_read = bytes_left_in_block;
        }

        // move to the correct disk
        uint32_t op = encode_op(JBOD_SEEK_TO_DISK, disk_num & 0x0F, 0);
        if (jbod_operation(op, NULL) != 0) {
            return -4;
        }

        // move to the correct block
        op = encode_op(JBOD_SEEK_TO_BLOCK, 0, block_num & 0xFF);
        if (jbod_operation(op, NULL) != 0) {
            return -4;
        }

        // read block data
        uint8_t block[JBOD_BLOCK_SIZE];
        op = encode_op(JBOD_READ_BLOCK, 0, 0);
        if (jbod_operation(op, block) != 0) {
            return -4; 
        }

        // copy the data from block to buffer
        memcpy(read_buf + bytes_read, block + offset_in_block, bytes_to_read);

        // update total bytes read
        bytes_read += bytes_to_read;
    }

    return bytes_read;
}
