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

#ifndef __LIB_SMU_H__
#define __LIB_SMU_H__

#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* Version the loaded driver must use to be compatible. */
#define LIBSMU_SUPPORTED_DRIVER_VERSION                    "0.1.7"

/**
 * SMU Mailbox Target
 */
enum smu_mailbox {
    SMU_TYPE_RSMU,
    SMU_TYPE_MP1,
    SMU_TYPE_HSMP,
};

/**
 * Return values that can be sent from the SMU in response to a command.
 */
typedef enum {
    SMU_Return_OK                = 0x01,
    SMU_Return_Failed            = 0xFF,
    SMU_Return_UnknownCmd        = 0xFE,
    SMU_Return_CmdRejectedPrereq = 0xFD,
    SMU_Return_CmdRejectedBusy   = 0xFC,

    // Custom Error Code -- Does not exist in SMU.

    // SMU Management failed to respond within the SMU_TIMEOUT_MS range.
    SMU_Return_CommandTimeout    = 0xFB,
    // An invalid argument was sent to the function.
    SMU_Return_InvalidArgument   = 0xFA,
    // Function is unsupported on the current processor.
    SMU_Return_Unsupported       = 0xF9,
    // Insufficient buffer size specified.
    SMU_Return_InsufficientSize  = 0xF8,
    // Failed to map physical address.
    SMU_Return_MappedError       = 0xF7,
    // PCIe programming error.
    SMU_Return_PCIFailed         = 0xF6,

    // Userspace Library Codes

    // Driver is not currently loaded or inaccessible.
    SMU_Return_DriverNotPresent  = 0xF0,
    // Read or write error has occurred. Check errno for last error.
    SMU_Return_RWError           = 0xE9,
    // Driver version is incompatible.
    SMU_Return_DriverVersion     = 0xE8,
} smu_return_val;

/**
 * Supported processor codenames with SMU capabilities.
 */
typedef enum {
    CODENAME_UNDEFINED,
    CODENAME_COLFAX,
    CODENAME_RENOIR,
    CODENAME_PICASSO,
    CODENAME_MATISSE,
    CODENAME_THREADRIPPER,
    CODENAME_CASTLEPEAK,
    CODENAME_RAVENRIDGE,
    CODENAME_RAVENRIDGE2,
    CODENAME_SUMMITRIDGE,
    CODENAME_PINNACLERIDGE,
    CODENAME_REMBRANDT,
    CODENAME_VERMEER,
    CODENAME_VANGOGH,
    CODENAME_CEZANNE,
    CODENAME_MILAN,
    CODENAME_DALI,
    CODENAME_LUCIENNE,
    CODENAME_NAPLES,
    CODENAME_CHAGALL,
    CODENAME_RAPHAEL,
    CODENAME_PHOENIX,
    CODENAME_STRIXPOINT,
    CODENAME_GRANITERIDGE,
    CODENAME_HAWKPOINT,
    CODENAME_STORMPEAK,
    CODENAME_COUNT
} smu_processor_codename;

/**
 * SMU MP1 Interface Version [v9-v13]
 */
typedef enum {
    IF_VERSION_9,
    IF_VERSION_10,
    IF_VERSION_11,
    IF_VERSION_12,
    IF_VERSION_13,

    IF_VERSION_COUNT
} smu_if_version;

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
const char* smu_get_fw_version(smu_obj_t* obj);

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
smu_return_val smu_send_command(smu_obj_t* obj, unsigned int op, smu_arg_t *args,
    enum smu_mailbox mailbox);

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
const char* smu_codename_to_str(smu_obj_t* obj);

/**
 * Determines whether PM tables are supported.
 * Returns 1 if they are.
 */
unsigned int smu_pm_tables_supported(smu_obj_t* obj);

#endif /* __LIB_SMU_H__ */
