#pragma once

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
 * SMU Mailbox Target
 */
typedef enum {
    MAILBOX_TYPE_RSMU,
    MAILBOX_TYPE_MP1,
    MAILBOX_TYPE_HSMP,

    MAILBOX_TYPE_COUNT
} smu_mailbox;

const char* get_code_name(smu_processor_codename codename);
