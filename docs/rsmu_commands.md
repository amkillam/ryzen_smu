# RSMU Commands

The following commands are known to be valid for RSMU supported processors. The table below
indicates various mnemonics and their meanings. Optional details may be shown if applicable.

| **Mnemonic** | **Description**                                            |
| :----------: | ---------------------------------------------------------- |
|  PBO Scalar  | Precision Boost Overdrive Aggressiveness                   |
|     PBO      | Precision Boost Overdrive                                  |
|     PPT      | Package Power Tracking                                     |
|     TDC      | Thermal Design Current                                     |
|     EDC      | Electrical Design Current                                  |
|    AutoOC    | Automatic Overclocking                                     |
|     ArgX     | Command Request Argument X (1-6)                           |
|     ResX     | Command Response Argument X (1-6)                          |
|      mW      | Milliwatt                                                  |
|      mA      | Milliampere                                                |
|      C       | Celcius                                                    |
|     VID      | Voltage ID Index (Voltage Applied = 1.55 (VID \* 0.00625)) |
|   PhyAddr    | Physical Address                                           |
|   BitField   | Bit Field Containing Values                                |

# Global Commands

The following commands work on all processors supporting MP1 or RSMU:

| **Function**  | **Command ID** | **Details**        |
| :-----------: | :------------: | ------------------ |
|  TestMessage  |      0x01      | Arg0:X, Res0:(X+1) |
| GetSMUVersion |      0x02      | Res0:Version       |

## Matisse & Vermeer

<small>_Commands shown to be used in Ryzen Master._</small>

|       **Function**        | **Command ID** | **Details**                      |
| :-----------------------: | :------------: | -------------------------------- | ------------------------------- |
|   TransferTableSmu2Dram   |      0x05      |                                  |
|    GetDramBaseAddress     |      0x06      | Res0:PhyAddr                     |
|     GetPMTableVersion     |      0x08      | Res0:Version                     |
|        SetVDDCRSoC        |      0x14      | Arg0:VID                         |
|        SetPPTLimit        |      0x53      | Arg0:mW                          |
|        SetTDCLimit        |      0x54      | Arg0:mA                          |
|        SetEDCLimit        |      0x55      | Arg0:mA                          |
|       SetcHTCLimit        |      0x56      | Arg0:C                           |
|       SetPBOScalar        |      0x58      | Arg0:(PBO Scalar \* 100)         |
|  GetFastestCoreOfSocket   |      0x59      | Res0:(16 \* BYTE2(Res0))         | (Res0 + 4 \* BYTE1(Res0)) & 0xF |
|     SetPROCHOTStatus      |      0x5A      | Arg0:Status, Disabled(0x1000000) |
|    EnableOverclocking     |      0x5A      | Arg0:Status, Enabled(0)          |
|    DisableOverclocking    |      0x5B      | Arg0:Status, Disabled(0x1000000) |
| SetOverclockFreqAllCores  |      0x5C      | Arg0:Frequency                   |
|  SetOverclockFreqPerCore  |      0x5D      | Arg0:Value                       |
|    SetOverclockCPUVID     |      0x61      | Arg0:VID                         |
|       GetPBOScalar        |      0x6C      | Res0:PBO Scalar:(1-10)           |
|      GetMaxFrequency      |      0x6E      | Res0:MHz                         |
| GetProcessorParameters(?) |      0x6F      | Res0:BitField                    |

### GetProcessorParameters Bit Field

| Bit |        Description         |
| :-: | :------------------------: |
|  0  |      IsOverclockable       |
|  1  | FusedMOCStatus/PBO Support |

### SetOverclockFreqPerCore Value Mask

#### Architectural Note

Generally, multiple cores are contained within a core-complex (CCX), one or many CCXs are contained within a CPU complex die (CCD), and one or many CCDs are contained
within a central processing unit (CPU).

As a rule, all cores within the same CCX of a CCD should generally be set to the same frequency. Otherwise, anecdotally, actual outputted frequency is unpredictable.

#### Variable Definitions

All below indices and count values are zero-indexed.

- let `frequency` be the intended frequency for the specified core.
- let `core_id` be the index of the core within its containing CCX.
- let `core_complex` be the index of the CCX within its containing CCD.
- let `ccd_id` be the index of the CCD within its CPU.
- let `MAX_CORE_FREQ` be the maximum allowed of frequency for a core in the specified CPU.
- let `CORE_COUNT_PER_CCX` be the number of cores in per CCX in the specified CPU.
- let `CCX_COUNT_PER_CCD` be the number of CCXs in a CCD in the specified CPU.
- let `CCD_COUNT_PER_CPU` be the number of CCDs in the specified CPU.

Value supplied is calculated as follows, with all of `CORE_COUNT_PER_CCX`, `CCX_COUNT_PER_CCD`, and `CCD_COUNT_PER_CPU` being zero-indexed values:

#### Value Mask Operations as a Function

##### Example Function

```c
unsigned int mask_core_freq(unsigned short int frequency, char core_id, char core_complex, char ccd_id) {
    // Ensures all arguments are within allowed bounds
    if (frequency > MAX_CORE_FREQ || core_id > CORE_COUNT_PER_CCX || core_complex > CCX_COUNT_PER_CCD || ccd_id > CCD_COUNT_PER_CPU)
        exit(0xDEAD);

    return frequency & 0xFFFFF | ((core_id & 0xF | (16 * (core_complex & 0xF | (16 * ccd_id)))) << 20);
}
```

##### Example Usage

The following is an example usage of the mask_core_freq with an AMD Ryzen 7950x CPU.

Maximum core frequency is an AMD configured value, and in this example, is assumed to have the same value in Raphael as the known value of 8000MHz in Matisse and Vermeer.

The 7950x has 16 (15 zero-indexed) total cores, structured as:

- 2 (1 zero-indexed) CCDs per CPU
- 1 (0 zero-indexed) CCX per CPU
- 8 (7 zero-indexed) cores per CCX

```c

//Specific for AMD Ryzen 7950x CPU
#define MAX_CORE_FREQ 8000
#define CORE_COUNT_PER_CCX 7
#define CCX_COUNT_PER_CCD 0
#define CCD_COUNT_PER_CPU 0

unsigned int mask_core_freq(unsigned short int frequency, char core_id, char core_complex, char ccd_id) {
    /* ... */
}

int main() {

    /*
    Sets the first core (core 0) to frequency of 5250MHz.
    Core 0 is the first (0) core of the first (0) CCX of the first (0) CCD.
    */
    unsigned short int frequency = 5250;
    char core_id = 0;
    char core_complex = 0;
    char ccd_id = 0;
    unsigned int first_core_freq_masked = mask_core_freq(frequency, core_id, core_complex, ccd_id);

    /*
    Sets the 9th core (core 8) to a frequency of 5250MHz.
    Core 8 is the first (0) core in the first (0) CCX of the second (1) CCD.
    */
    unsigned short int frequency = 5250;
    char core_id = 0
    char core_complex = 0;
    char ccd_id = 1;
    unsigned int ninth_core_freq_masked = mask_core_freq(frequency, core_id, core_complex, ccd_id);

    /*
    Sets the 16th core (core 15) to a frequency of 5250MHz.
    Core 15 is the 8th (7) core in the first (0) CCX of the second(1) CCD.
    Core 15 is within the same CCX and CCD as core 8, which has already been configured to 5250MHz. Core 15 must either be configured to the same frequency of 5250MHz,
    or both configured to a new, but identical value.
    */
    unsigned short int frequency = 5250;
    char core_id = 7;
    char core_complex = 0;
    char ccd_id = 1;
    unsigned int sixteenth_core_freq_masked = mask_core_freq(frequency, core_id, core_complex, ccd_id);
}

```

### Argument Format

Unless a different formatting or mask is specified otherwise, function arguments must be formatted as follows:

1. Convert argument to hex
2. Pad hex value to a length of 48 bits
3. Split padded value to two-bit chunks
4. Reverse order of chunks - hex argument value will be first chunk.

#### Bash Example

The following bash code, with an inputted VID of 150<sub>10</sub> (96<sub>16</sub>), will give `FORMATTED_ARG` a binary value with a two-byte hexadecimal formatted hexdump representation of:

```hex
0000000    0096    0000    0000    0000    0000    0000    0000    0000
0000010    0000    0000    0000    0000
0000018
```

```bash
ARG=150
FORMATTED_ARG=$(printf '%0*x' 48 $ARG | fold -w 2 | tac | tr -d '\n' | xxd -r -p)
```

##### Steps

The following outlines the operation performed at each step of `FORMATTED_ARG`'s computation:

1. `printf '%0*x' 48 $ARG`: converts ARG to hex and pads to length of 48 bits.
2. `fold -w 2`: splits to two-bit chunks, one chunk on each line
3. `tac`: Reverses order of lines (chunks)
4. `tr -d '\n'`: Removes new lines - moves all bits to one line.
5. `xxd -r -p`: parses hex-formatted string representation of ARG bytes, converts to raw binary value.

### Configuring Load Core Voltage

#### Accounting For V<sub>droop</sub>

The RSMU function SetOverclockCPUVID configures the _idle_ VID for all cores. VID will instaneously decrease/increase under load depending on your motherboard's configured V<sub>droop</sub>
(often configured via the Load-Line Calibration setting in BIOS menus), instaneous average core frequency, and countless other factors.

While V<sub>droop</sub> cannot be directly configured, but its effect proportional to any given instaneous average core frequency can be determined to an applicable degree of accuracy, and thus
accounted for via the following process.

Proceed with caution, as a sufficiently large V<sub>droop</sub> can cause a dangerously high idle voltage, when applied VID is incremented to account for the large V<sub>droop</sub>.

Note: RSMU functions will be referred to by name, and calling the functions correctly is left as an exercise to the reader.

##### Data Collection Steps

###### Initial Measurements

1. Set all-core overclock frequency to a relatively low value for your platform.
2. Set VID to a safe and stable value for your platform and configured frequency.
3. Ensure cores are idle (frequencies rapidly fluctuating at values well-below configured frequency, generally at < 1000MHz ). Record real, applied VID to cores.
4. Run an all-core, CPU-intensive software. Record the real, applied VID to cores.
5. Determine the difference between VID values recorded in steps 3 and 4.
6. Divide configured frequency by the difference calculated in step 5, then record value.

###### Calibration

7. Determine a new frequency to configure.
8. Repeat steps 1-6 with the new frequency determined in step 7.
9. Repeat step 8 for five iterations.
10. Find the rounded average of resulting values of step 6 in the aforementioned five iterations and initial measurement.

###### V<sub>droop</sub> Calculation

Given results of the data collection, we can now calculate V<sub>droop</sub> for an instaneous average core frequency, and account for it in applied VID.

The variables used in calculation are defined below:

- Let `let step_six_avg` be the results of the data collection.
- Let `let avg_core_freq` be the average of the applied frequencies to all cores.
- Let `let intended_vid` be the intended VID to be used when CPU is under load.
- Let `let incremented_vid` be the VID value used in the SetOverclockCPUVID function call argument, which accounts for V<sub>droop</sub> and ensures VID used under load is equal to intended_vid.

`incremented_vid` is then finally calculated as follows:
`incremented_vid = intended_vid + (avg_core_freq/step_six_avg)`

##### Example

The following example outlines the steps taken as outlined above for an AMD Ryzen 7950x, and the determined results.

###### Data Collection

1. Set frequency to 3800MHz.
2. Set VID to 180.
3. Used application corefreq to determine real, applied VID in idle was 180.
4. Load was applied to cores using Y-cruncher. VID under load was determined to be 174.
5. 180 - 174 = 6
6. 3800/6 = 600 + (100/3) -> 633.333333...

###### Results

- VID argument: 180
- Frequency inputs: 3800, 4050, 4300, 4550, 4800, 5050, 5250
- Step 5 results: 6, 7, 7, 7, 8, 8, 8
- Step 6 results: 633.33, 578.57, 614.29, 650, 600, 656.25
  Average of step 6 results, rounded: 622

Incremented VID values were then calculated and used, consistently achieving intended VID under load.

Notes:

- Discrepancies between step 6 results are assumed to be the result of rounding float values to integers. The average of these values allows calculation of a reasonable approximation of
  real V<sub>droop</sub>, though the formula used internally is currently unknown.
- Referring to Raphael voltage calculation formula shown below, featuring a constant multiplier of `0.00625`, the following formula, anecdotally, also correctly calculated V<sub>droop</sub>,
  though the relation and domain of applicability is currently uncertain - `incremented_vid = 0.25 * 0.00625 * intended_vid`.

### Raphael

All above information should generally apply to Raphael as well, with the following known differences:

- Applied voltage is now calculated as: `Voltage = 0.00625 * VID`, where VID is a decimal value, and Voltage is measured in volts.
- EnableOverclocking command ID is now 0x5D
- DisableOverclocking command ID is now 0x5E
- SetOverclockFreqAllCores command ID is now 0x5F
- SetOverclockFreqPerCore command ID is now 0x60
