/**
 * Ryzen SMU Userspace Library
 * Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#pragma once

#include <pthread.h>

#include "../smu_common.h"

/* Version the loaded driver must use to be compatible. */
#define LIBSMU_SUPPORTED_DRIVER_VERSION                    "0.1.7"

/**
 * Mutex lock enumeration for specific components.
 */
enum SMU_MUTEX_LOCK {
    SMU_MUTEX_SMN,
    SMU_MUTEX_CMD,
    SMU_MUTEX_PM,
    SMU_MUTEX_COUNT
};

typedef struct {
    /* Accessible To Users, Read-Only. */
    unsigned int                init;
    unsigned int                driver_version;

    smu_processor_codename      codename;
    smu_if_version              smu_if_version;
    unsigned int                smu_version;
    unsigned int                pm_table_size;
    unsigned int                pm_table_version;

    /* Internal Library Use Only */
    int                         fd_smn;
    int                         fd_rsmu_cmd;
    int                         fd_mp1_smu_cmd;
    int                         fd_hsmp_smu_cmd;
    int                         fd_smu_args;
    int                         fd_pm_table;

    pthread_mutex_t             lock[SMU_MUTEX_COUNT];
} smu_obj_t;

typedef union {
    struct {
        float                   args0_f;
        float                   args1_f;
        float                   args2_f;
        float                   args3_f;
        float                   args4_f;
        float                   args5_f;
    } f;
    struct {
        unsigned int            args0;
        unsigned int            args1;
        unsigned int            args2;
        unsigned int            args3;
        unsigned int            args4;
        unsigned int            args5;
    } i;

    unsigned int                args[6];
    float                       args_f[6];
} smu_arg_t;

/**
 * Initializes or frees the userspace library for use.
 * Upon successful initialization, users are allowed to access the following members:
 *  - codename
 *  - smu_if_version
 *  - smu_version
 *  - pm_table_size
 *  - pm_table_version
 *
 * Returns SMU_Return_OK on success.
 */
smu_return_val smu_init(smu_obj_t* obj);
void smu_free(smu_obj_t* obj);

/**
 * Returns the string representation of the SMU FW version.
 */
const char* smu_get_fw_version(const smu_obj_t* obj);

/**
 * Reads or writes a 32 bit word from the SMN address space.
 */
smu_return_val smu_read_smn_addr(smu_obj_t* obj, unsigned int address, unsigned int* result);
smu_return_val smu_write_smn_addr(smu_obj_t* obj, unsigned int address, unsigned int value);

/**
 * Sends a command to the SMU.
 * Arguments are sent in the args buffer and are also returned in it.
 *
 * Returns SMU_Return_OK on success.
 */
smu_return_val smu_send_command(smu_obj_t* obj, unsigned int op, smu_arg_t *args, smu_mailbox mailbox);

/**
 * Reads the PM table into the destination buffer.
 *
 * Returns an SMU_Return_OK on success.
 */
smu_return_val smu_read_pm_table(smu_obj_t* obj, unsigned char* dst, size_t dst_len);

/** HELPER METHODS **/

/**
 * Converts SMU values to the string representation.
 */
const char* smu_return_to_str(smu_return_val val);
const char* smu_codename_to_str(const smu_obj_t* obj);

/**
 * Determines whether PM tables are supported.
 * Returns 1 if they are.
 */
unsigned int smu_pm_tables_supported(const smu_obj_t* obj);
