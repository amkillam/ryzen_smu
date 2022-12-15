# RSMU Commands

The following commands are known to be valid for RSMU supported processors. The table below
indicates various mnemonics and their meanings. Optional details may be shown if applicable.

| **Mnemonic**  | **Description**                                                                       |
|:-------------:| ------------------------------------------------------------------------------------- |
| PBO Scalar    | Precision Boost Overdrive Aggressiveness                                              |
| PBO           | Precision Boost Overdrive                                                             |
| PPT           | Package Power Tracking                                                                |
| TDC           | Thermal Design Current                                                                |
| EDC           | Electrical Design Current                                                             |
| AutoOC        | Automatic Overclocking                                                                |
| ArgX          | Command Request Argument X (1-6)                                                      |
| ResX          | Command Response Argument X (1-6)                                                     |
| mW            | Milliwatt                                                                             |
| mA            | Milliampere                                                                           |
| C             | Celcius                                                                               |
| VID           | Voltage ID Index (Value Applied = 1.55 - (VID * 0.00625))                             |
| PhyAddr       | Physical Address                                                                      |
| BitField      | Bit Field Containing Values                                                           |

# Global Commands

The following commands work on all processors supporting MP1 or RSMU:

| **Function**              | **Command ID** | **Details**                                              |
|:-------------------------:|:--------------:| -------------------------------------------------------- |
| TestMessage               | 0x01           | Arg0:X, Res0:(X+1)                                       |
| GetSMUVersion             | 0x02           | Res0:Version                                             |

## Matisse & Vermeer

<small>*Commands shown to be used in Ryzen Master.*</small>

| **Function**              | **Command ID** | **Details**                                              |
|:-------------------------:|:--------------:| -------------------------------------------------------- |
| TransferTableSmu2Dram     | 0x05           |                                                          |
| GetDramBaseAddress        | 0x06           | Res0:PhyAddr                                             |
| GetPMTableVersion         | 0x08           | Res0:Version                                             |
| SetVDDCRSoC               | 0x14           | Arg0:VID                                                 |
| SetPPTLimit               | 0x53           | Arg0:mW                                                  |
| SetTDCLimit               | 0x54           | Arg0:mA                                                  |
| SetEDCLimit               | 0x55           | Arg0:mA                                                  |
| SetcHTCLimit              | 0x56           | Arg0:C                                                   |
| SetPBOScalar              | 0x58           | Arg0:(PBO Scalar * 100)                                  |
| GetFastestCoreOfSocket    | 0x59           | Res0:(16 * BYTE2(Res0)) | (Res0 + 4 * BYTE1(Res0)) & 0xF |
| SetPROCHOTStatus          | 0x5A           | Arg0:Status, Disabled(0x1000000)                         |
| EnableOverclocking        | 0x5A           | Arg0:Status, Enabled(0)                                  |
| DisableOverclocking       | 0x5B           | Arg0:Status, Disabled(0x1000000)                         |
| SetOverclockFreqAllCores  | 0x5C           | Arg0:Frequency                                           |
| SetOverclockFreqPerCore   | 0x5D           | Arg0:Value                                               |
| SetOverclockCPUVID        | 0x61           | Arg0:VID                                                 |
| GetPBOScalar              | 0x6C           | Res0:PBO Scalar:(1-10)                                   |
| GetMaxFrequency           | 0x6E           | Res0:MHz                                                 |
| GetProcessorParameters(?) | 0x6F           | Res0:BitField                                            |

### GetProcessorParameters Bit Field

| Bit  | Description                 |
|:----:|:---------------------------:|
| 0    | IsOverclockable             |
| 1    | FusedMOCStatus/PBO Support  |

### SetOverclockFreqPerCore Value Mask

Value supplied is calculated as follows:

```cpp
// Invalid.
if (frequency > 8000 || core_id > 3 || core_complex > 1 || ccd_id > 1)
    exit(0xDEAD);

value = frequency & 0xFFFFF | ((core_id & 0xF | (16 * (core_complex & 0xF | (16 * ccd_id)))) << 20)
```
