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

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "libsmu.h"

#define DRIVER_CLASS_PATH               "/sys/kernel/ryzen_smu_drv/"

#define DRIVER_VERSION_PATH             DRIVER_CLASS_PATH "drv_version"
#define VERSION_PATH                    DRIVER_CLASS_PATH "version"
#define IF_VERSION_PATH                 DRIVER_CLASS_PATH "mp1_if_version"
#define CODENAME_PATH                   DRIVER_CLASS_PATH "codename"

#define SMN_PATH                        DRIVER_CLASS_PATH "smn"
#define SMU_ARG_PATH                    DRIVER_CLASS_PATH "smu_args"
#define RSMU_CMD_PATH                   DRIVER_CLASS_PATH "rsmu_cmd"
#define MP1_SMU_CMD_PATH                DRIVER_CLASS_PATH "mp1_smu_cmd"
#define HSMP_SMU_CMD_PATH               DRIVER_CLASS_PATH "hsmp_smu_cmd"

#define PM_VERSION_PATH                 DRIVER_CLASS_PATH "pm_table_version"
#define PM_SIZE_PATH                    DRIVER_CLASS_PATH "pm_table_size"
#define PM_PATH                         DRIVER_CLASS_PATH "pm_table"

/* Maximum driver version length defined as "255.255.255\n" */
#define LIBSMU_MAX_DRIVER_VERSION_LEN   12

/* Maximum is defined as: "255.255.255.255\n" */
#define LIBSMU_MAX_SMU_VERSION_LEN      16

static int try_open_path(const char* pathname, int mode, int* fd) {
    int ret = 1;

    *fd = open(pathname, mode);

    // Reset fd to zero to avoid attempting to close a -1 file descriptor.
    if (*fd == -1)
        ret = *fd = 0;

    return ret;
}

static smu_return_val smu_init_parse(smu_obj_t* obj) {
    int ver_maj, ver_min, ver_rev, ver_alt, len, i, c;
    char rd_buf[1024];
    int tmp_fd, ret;

    memset(rd_buf, 0, sizeof(rd_buf));

    // Verify the driver version is expected.
    if (!try_open_path(DRIVER_VERSION_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_DriverNotPresent;

    ret = read(tmp_fd, rd_buf, LIBSMU_MAX_DRIVER_VERSION_LEN);
    close(tmp_fd);

    if (ret < 0)
        return SMU_Return_RWError;

    // The driver version must match the expected exactly.
    if (strcmp(rd_buf, LIBSMU_SUPPORTED_DRIVER_VERSION "\n"))
        return SMU_Return_DriverVersion;

    sscanf(rd_buf, "%d.%d.%d\n", &ver_maj, &ver_min, &ver_rev);
    obj->driver_version = ver_maj << 16 | ver_min << 8 | ver_rev;

    // The version of the SMU **MUST** be present.
    if (!try_open_path(VERSION_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_DriverNotPresent;

    ret = read(tmp_fd, rd_buf, LIBSMU_MAX_SMU_VERSION_LEN);
    close(tmp_fd);

    if (ret < 0)
        return SMU_Return_RWError;

    len = strlen(rd_buf);
    for (i = 0, c = 0; i < len; i++)
        if (rd_buf[i] == '.')
            c++;

    // Depending on the processor, there can be either a 3 or 4 part version segmentation.
    // We account for both.
    switch (c) {
        case 2:
            ret = sscanf(rd_buf, "%d.%d.%d\n", &ver_maj, &ver_min, &ver_rev);
            obj->smu_version = ver_maj << 16 | ver_min << 8 | ver_rev;
            break;
        case 3:
            ret = sscanf(rd_buf, "%d.%d.%d.%d\n", &ver_maj, &ver_min, &ver_rev, &ver_alt);
            obj->smu_version = ver_maj << 24 | ver_min << 16 | ver_rev << 8 | ver_alt;
            break;
        default:
            return SMU_Return_RWError;
    }

    if (ret == EOF || ret < 3)
        return SMU_Return_RWError;

    // Codename must also be present.
    if (!try_open_path(CODENAME_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_DriverNotPresent;

    ret = read(tmp_fd, rd_buf, 3);
    close(tmp_fd);

    if (ret < 0)
        return SMU_Return_RWError;

    obj->codename = atoi(rd_buf);

    if (obj->codename <= CODENAME_UNDEFINED ||
        obj->codename >= CODENAME_COUNT)
        return SMU_Return_Unsupported;

    // MP1 version must also be present.
    if (!try_open_path(IF_VERSION_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_DriverNotPresent;

    // This only specifies an enumeration for the IF version.
    ret = read(tmp_fd, rd_buf, sizeof(rd_buf));
    close(tmp_fd);

    if (ret < 0)
        return SMU_Return_RWError;

    ret = sscanf(rd_buf, "%d\n", (int*)&obj->smu_if_version);
    if (ret == EOF || ret > 3)
        return SMU_Return_RWError;

    // This file doesn't need to exist if PM Tables aren't supported.
    if (!try_open_path(PM_VERSION_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_OK;

    ret = read(tmp_fd, &obj->pm_table_version, sizeof(obj->pm_table_version));
    close(tmp_fd);

    if (ret <= 0)
        return SMU_Return_RWError;

    // If the PM table contains a version, a size file MUST exist.
    if (!try_open_path(PM_SIZE_PATH, O_RDONLY, &tmp_fd))
        return SMU_Return_RWError;

    ret = read(tmp_fd, &obj->pm_table_size, sizeof(obj->pm_table_size));
    close(tmp_fd);

    if (ret <= 0)
        return SMU_Return_RWError;

    return SMU_Return_OK;
}

smu_return_val smu_init(smu_obj_t* obj) {
    int i, ret;

    memset(obj, 0, sizeof(*obj));

    // Parse constants: SMU Version, Processor Codename, PM Table Size/Version
    ret = smu_init_parse(obj);
    if (ret != SMU_Return_OK)
        return ret;

    // The driver must provide access to these files.
    if (!try_open_path(SMN_PATH, O_RDWR, &obj->fd_smn) ||
        !try_open_path(MP1_SMU_CMD_PATH, O_RDWR, &obj->fd_mp1_smu_cmd) ||
        !try_open_path(HSMP_SMU_CMD_PATH, O_RDWR, &obj->fd_hsmp_smu_cmd) ||
        !try_open_path(SMU_ARG_PATH, O_RDWR, &obj->fd_smu_args))
        return SMU_Return_RWError;

    // RSMU is optionally supported for some codenames.
    if (try_open_path(RSMU_CMD_PATH, O_RDWR, &obj->fd_rsmu_cmd)) {
        // This file may optionally exist only if PM tables are supported AND RSMU as well.
        if (smu_pm_tables_supported(obj) &&
            !try_open_path(PM_PATH, O_RDONLY, &obj->fd_pm_table))
            return SMU_Return_RWError;
    }

    for (i = 0; i < SMU_MUTEX_COUNT; i++)
        pthread_mutex_init(&obj->lock[i], NULL);

    obj->init = 1;

    return SMU_Return_OK;
}

void smu_free(smu_obj_t* obj) {
    int i;

    if (!obj->init)
        return;

    if (obj->fd_smn)
        close(obj->fd_smn);

    if (obj->fd_rsmu_cmd)
        close(obj->fd_rsmu_cmd);

    if (obj->fd_mp1_smu_cmd)
        close(obj->fd_mp1_smu_cmd);

    if (obj->fd_hsmp_smu_cmd)
        close(obj->fd_hsmp_smu_cmd);

    if (obj->fd_smu_args)
        close(obj->fd_smu_args);

    if (obj->fd_pm_table)
        close(obj->fd_pm_table);

    for (i = 0; i < SMU_MUTEX_COUNT; i++)
        pthread_mutex_destroy(&obj->lock[i]);

    memset(obj, 0, sizeof(*obj));
}

const char* smu_get_fw_version(smu_obj_t* obj) {
    static char fw[32] = { 0 };

    if (!obj->init)
        return "Uninitialized";

    // Determine if this is a 24-bit or 32-bit version and show it accordingly.
    if (obj->smu_version & 0xff000000) {
        sprintf(fw, "%d.%d.%d.%d",
            (obj->smu_version >> 24) & 0xff, (obj->smu_version >> 16) & 0xff,
            (obj->smu_version >> 8) & 0xff, obj->smu_version & 0xff);
    }
    else
        sprintf(fw, "%d.%d.%d",
            (obj->smu_version >> 16) & 0xff, (obj->smu_version >> 8) & 0xff,
            obj->smu_version & 0xff);

    return fw;
}

smu_return_val smu_read_smn_addr(smu_obj_t* obj, unsigned int address, unsigned int* result) {
    unsigned int ret;

    // Don't attempt to execute without initialization.
    if (!obj->init)
        return SMU_Return_Failed;

    pthread_mutex_lock(&obj->lock[SMU_MUTEX_SMN]);

    lseek(obj->fd_smn, 0, SEEK_SET);
    ret = write(obj->fd_smn, &address, sizeof(address));

    if (ret != sizeof(address))
        goto BREAK_OUT;

    lseek(obj->fd_smn, 0, SEEK_SET);
    ret = read(obj->fd_smn, result, sizeof(*result));

BREAK_OUT:
    pthread_mutex_unlock(&obj->lock[SMU_MUTEX_SMN]);

    return ret == sizeof(unsigned int) ? SMU_Return_OK : SMU_Return_RWError;
}

smu_return_val smu_write_smn_addr(smu_obj_t* obj, unsigned int address, unsigned int value) {
    unsigned int buffer[2], ret;

    // Don't attempt to execute without initialization.
    if (!obj->init)
        return SMU_Return_Failed;

    // buffer[0] contains the destination write target.
    // buffer[1] contains the value to write to the address.
    buffer[0] = address;
    buffer[1] = value;

    pthread_mutex_lock(&obj->lock[SMU_MUTEX_SMN]);

    lseek(obj->fd_smn, 0, SEEK_SET);
    ret = write(obj->fd_smn, buffer, sizeof(buffer));

    pthread_mutex_unlock(&obj->lock[SMU_MUTEX_SMN]);

    return ret == sizeof(buffer) ? SMU_Return_OK : SMU_Return_RWError;
}

smu_return_val smu_send_command(smu_obj_t* obj, unsigned int op, smu_arg_t* args,
    enum smu_mailbox mailbox) {
    unsigned int ret, status, fd_smu_cmd;

    // Don't attempt to execute without initialization.
    if (!obj->init)
        return SMU_Return_Failed;

    switch (mailbox) {
        case SMU_TYPE_RSMU:
            fd_smu_cmd = obj->fd_rsmu_cmd;
            break;
        case SMU_TYPE_MP1:
            fd_smu_cmd = obj->fd_mp1_smu_cmd;
            break;
        case SMU_TYPE_HSMP:
            fd_smu_cmd = obj->fd_hsmp_smu_cmd;
            break;
        default:
            return SMU_Return_Unsupported;
    }

    // Check if fd is valid.
    if (!fd_smu_cmd)
        return SMU_Return_Unsupported;

    pthread_mutex_lock(&obj->lock[SMU_MUTEX_CMD]);

    lseek(obj->fd_smu_args, 0, SEEK_SET);
    ret = write(obj->fd_smu_args, args->args, sizeof(*args));

    if (ret != sizeof(*args)) {
        ret = SMU_Return_RWError;
        goto BREAK_OUT;
    }

    lseek(fd_smu_cmd, 0, SEEK_SET);
    ret = write(fd_smu_cmd, &op, sizeof(op));

    if (ret != sizeof(op)) {
        ret = SMU_Return_RWError;
        goto BREAK_OUT;
    }

    // Commands should be completed instantly as the driver attempts to continuously
    //  execute it till a timeout has occurred and immediately updates the result.
    // Therefore it shouldn't be necessary to apply any sort of waiting here.
    lseek(fd_smu_cmd, 0, SEEK_SET);
    ret = read(fd_smu_cmd, &status, sizeof(status));

    if (ret != sizeof(status))
        ret = SMU_Return_RWError;
    else
        ret = status;

    if (ret == SMU_Return_OK) {
        lseek(obj->fd_smu_args, 0, SEEK_SET);
        ret = read(obj->fd_smu_args, args->args, sizeof(args->args)) == sizeof(args->args)
            ? SMU_Return_OK
            : SMU_Return_RWError;
    }

BREAK_OUT:
    pthread_mutex_unlock(&obj->lock[SMU_MUTEX_CMD]);

    return ret;
}

smu_return_val smu_read_pm_table(smu_obj_t* obj, unsigned char* dst, size_t dst_len) {
    int ret;

    // Don't attempt to execute without initialization.
    if (!obj->init)
        return SMU_Return_Failed;

    if (dst_len != obj->pm_table_size)
        return SMU_Return_InsufficientSize;

    pthread_mutex_lock(&obj->lock[SMU_MUTEX_PM]);

    lseek(obj->fd_pm_table, 0, SEEK_SET);
    ret = read(obj->fd_pm_table, dst, obj->pm_table_size);

    if (ret != obj->pm_table_size)
        ret = SMU_Return_RWError;
    else
        ret = SMU_Return_OK;

    pthread_mutex_unlock(&obj->lock[SMU_MUTEX_PM]);

    return ret;
}

const char* smu_return_to_str(smu_return_val val) {
    switch (val) {
        case SMU_Return_OK:
            return "OK";
        case SMU_Return_Failed:
            return "Failed";
        case SMU_Return_UnknownCmd:
            return "Unknown Command";
        case SMU_Return_CmdRejectedPrereq:
            return "Command Rejected - Prerequisite Unmet";
        case SMU_Return_CmdRejectedBusy:
            return "Command Rejected - Busy";
        case SMU_Return_CommandTimeout:
            return "Command Timed Out";
        case SMU_Return_InvalidArgument:
            return "Invalid Argument Specified";
        case SMU_Return_Unsupported:
            return "Unsupported Platform Or Feature";
        case SMU_Return_InsufficientSize:
            return "Insufficient Buffer Size Provided";
        case SMU_Return_MappedError:
            return "Memory Mapping I/O Error";
        case SMU_Return_PCIFailed:
            return "PCIe Programming Error";
        case SMU_Return_DriverNotPresent:
            return "SMU Driver Not Present Or Fault";
        case SMU_Return_RWError:
            return "Read Or Write Error";
        case SMU_Return_DriverVersion:
            return "SMU Driver Version Incompatible With Library Version";
        default:
            return "Unspecified Error";
    }
}

const char* smu_codename_to_str(smu_obj_t* obj) {
    switch (obj->codename) {
        case CODENAME_CASTLEPEAK:
            return "CastlePeak";
        case CODENAME_COLFAX:
            return "Colfax";
        case CODENAME_MATISSE:
            return "Matisse";
        case CODENAME_PICASSO:
            return "Picasso";
        case CODENAME_PINNACLERIDGE:
            return "Pinnacle Ridge";
        case CODENAME_RAVENRIDGE2:
            return "Raven Ridge 2";
        case CODENAME_RAVENRIDGE:
            return "Raven Ridge";
        case CODENAME_RENOIR:
            return "Renoir";
        case CODENAME_SUMMITRIDGE:
            return "Summit Ridge";
        case CODENAME_THREADRIPPER:
            return "Thread Ripper";
        case CODENAME_REMBRANDT:
            return "Rembrandt";
        case CODENAME_RAPHAEL:
            return "Raphael";
        case CODENAME_GRANITERIDGE:
            return "GraniteRidge";
        case CODENAME_VERMEER:
            return "Vermeer";
        case CODENAME_VANGOGH:
            return "Van Gogh";
        case CODENAME_CEZANNE:
            return "Cezanne";
        case CODENAME_MILAN:
            return "Milan";
        case CODENAME_DALI:
            return "Dali";
        case CODENAME_LUCIENNE:
            return "Lucienne";
        case CODENAME_NAPLES:
            return "Naples";
        case CODENAME_PHOENIX:
            return "Phoenix";
        case CODENAME_STRIXPOINT:
            return "Strix Point";
        case CODENAME_HAWKPOINT:
            return "Hawk Point";
        case CODENAME_STORMPEAK:
            return "Storm Peak";
        default:
            return "Undefined";
    }
}

unsigned int smu_pm_tables_supported(smu_obj_t* obj) {
    return obj->pm_table_size && obj->pm_table_version;
}
