/*
ADF (https://github.com/dejaniv/adf.git)
Copyright (C)2021 Dejan Ivkovic

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stddef.h>

#include <pru_cfg.h>
#include <pru_ctrl.h>
#include <rsc_types.h>

#include "adf.h"

#define	INS_PER_US 200           // 5ns per instruction

#define PC_FORMAT_SYNC_WORD 0x44895554
#define PRU_SHARED_MEM_ADDR 0x00010000

volatile register uint32_t __R30;
volatile register uint32_t __R31;

volatile TrackResult* trackResult = (volatile TrackResult *) PRU_SHARED_MEM_ADDR;

typedef enum { FALSE = 0, TRUE } boolean;

static int DEBUG_SIGNAL_ENABLED = 0;

inline void setDebugPinHigh() {
    __R30 |= (1 << 14);
}

inline void setDebugPinLow() {
    __R30 &= ~(1 << 14);
}

uint32_t readBit() {
    static int zeroCount = 0;
    static uint32_t cycleCount = 0;
    static int first = 1;
    
    if (zeroCount) {
        zeroCount--;
        return 0;
    }

    // wait until current low state is over
    while(!(__R31 & (1 << 15)));

    if (DEBUG_SIGNAL_ENABLED) {
        setDebugPinHigh();
    }

    // wait until current high state is over
    while(__R31 & (1 << 15));
    
    uint32_t newCycleCount = PRU0_CTRL.CYCLE;

    
    if (DEBUG_SIGNAL_ENABLED) {
        setDebugPinLow();
    }

    uint32_t diff;
    diff =  newCycleCount - cycleCount;

    ++trackResult->signalHistogram[diff < ADF_HISTOGRAM_BUCKETS ? diff : ADF_HISTOGRAM_BUCKETS - 1];
    cycleCount = newCycleCount;

    if (diff < trackResult->shortSignalTreshold) {
        if (!first) {
            trackResult->diagnostics |= ADF_DIAG_SHORT_IMPULSE;
        }
        zeroCount = 1;
    }
    else if (diff < trackResult->singleZeroGapTreshold) {
        zeroCount = 1;
    } else if (diff < trackResult->twoZerosGapTreshold) {
        zeroCount = 2;
    } else if (diff < trackResult->threeZerosGapTreshold) {
        zeroCount = 3;
    } else {
        if (!first) {
            trackResult->diagnostics |= ADF_DIAG_LONG_IMPULSE;
        }
        zeroCount = 3;
    }

    first = 0;

    return 1;
}

inline uint32_t swapByteOrder(uint32_t value) {
    return (value & 0xff000000) >> 24 |  (value & 0x00ff0000) >> 8
        | (value & 0x0000ff00) << 8 |  (value & 0x000000ff) << 24;

}

ResultCode readDataAndDecode(int size, uint32_t* buffer, uint32_t* checksum) {
    const uint32_t mask = AMD_AMIGA_EVEN_BIT_MASK;
    const int sizeInLongs = (size >> 2);

    if (size & 0x3) {
        return ADF_DATA_SIZE_ERROR;
    }

    /* read */
    int i, bit;
    uint32_t readLong = 0;
    uint32_t chk = 0;
    for (i = 0; i < sizeInLongs; ++i) {
        for (bit = 0; bit < 32; ++bit) {
            readLong <<= 1;
            readLong |= readBit();
        }
        chk ^= readLong;
        buffer[i] = readLong;
    }

    /* read & decode */
    for (i = 0; i < sizeInLongs; ++i) {
        for (bit = 0; bit < 32; ++bit) {
            readLong <<= 1;
            readLong |= readBit();
        }

        chk ^= readLong;

        /* odd bits | even bits */
        buffer[i] = swapByteOrder(((buffer[i] & mask) << 1) | (readLong & mask));
    }

    if (checksum) {
        (*checksum) ^= swapByteOrder(chk & AMD_AMIGA_EVEN_BIT_MASK);
    }

    return ADF_SUCCESS;
}

void findFilling() {
    uint32_t word = 0;
    while (word != AMD_AMIGA_GAP_FILLER) {
        word <<= 1;
        word |= readBit();
    }
}

void findSectorSync() {
    uint32_t word = 0;
    while (word != AMD_AMIGA_SECTOR_SYNC) {
        word <<= 1;
        word |= readBit();
    }
}

ResultCode checkPcFormat()
{
    uint32_t magicWord;
    findSectorSync();
    int i;
    for (i = 0; i < 32; ++i) {
        magicWord <<= 1;
        magicWord |= readBit();
    }
    return (magicWord == PC_FORMAT_SYNC_WORD) ? ADF_PC_DISK_DETECTED : ADF_SUCCESS;
}

#define readDecodeCheck(dataField, checksum) \
                                    { \
                                        ResultCode result; \
                                        if ((result = readDataAndDecode(sizeof(*dataField), (uint32_t *)dataField, checksum)) != ADF_SUCCESS) { \
                                            return result; \
                                         } \
                                    } \

ResultCode readSector(Sector* sector, uint32_t *readMask) {
    DEBUG_SIGNAL_ENABLED = 1;
    sector->calculatedHeaderChecksum = 0;
	sector->calculatedDataChecksum = 0;
    findSectorSync();
    readDecodeCheck(&sector->header.info, &sector->calculatedHeaderChecksum);
    readDecodeCheck(&sector->header.label, &sector->calculatedHeaderChecksum);
    readDecodeCheck(&sector->headerChecksum, NULL);
    readDecodeCheck(&sector->dataChecksum, NULL);
    readDecodeCheck(&sector->data, &sector->calculatedDataChecksum);
    DEBUG_SIGNAL_ENABLED = 0;
    ResultCode result = ADF_SUCCESS;
    if (sector->calculatedHeaderChecksum != sector->headerChecksum) {
        result = ADF_CHECKSUM_ERROR;
        trackResult->diagnostics |= ADF_DIAG_HEADER_CHECKSUM_ERROR; 
    }
    if (sector->calculatedDataChecksum != sector->dataChecksum) {
        result = ADF_CHECKSUM_ERROR;
        trackResult->diagnostics |= ADF_DIAG_DATA_CHECKSUM_ERROR; 
    }
    if (result == ADF_SUCCESS) {
        *readMask |= (1 << sector->header.info.sectorNumber);
    }
    return result;
}

void main(void) {
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

    for(;;) {
        setDebugPinHigh();

        /* Wait for main program to tell us to run */
        while (trackResult->resultCode == ADF_STAND_BY);
        while (trackResult->resultCode != ADF_RUNNING);

        /* Enable and reset cycle counter */
        PRU0_CTRL.CTRL_bit.CTR_EN = 0;
        PRU0_CTRL.CTRL_bit.CTR_EN = 1;

        ResultCode sectorResult = ADF_SUCCESS;
        ResultCode result = ADF_SUCCESS;
        DEBUG_SIGNAL_ENABLED = 0;
        findFilling();

        uint32_t count = 0;
        uint32_t i;
        for (i = 0; i < ADF_MAX_SECTOR_READ_ATTEMPTS && count < ADF_TRACK_RESULT_SECTOR_COUNT; ++i) {
            Sector *sector = (Sector*)&trackResult->sectors[count];
            uint32_t mask = trackResult->readSectorMask;
            if ((sectorResult = readSector(sector, (uint32_t*)&trackResult->readSectorMask)) == ADF_SUCCESS) {
              if (mask != trackResult->readSectorMask) {
                ++count;
              }
            } else {
                result = sectorResult;
            }
        }
        
        // if we had no luck at all maybe we are looking at
        // PC disk format
        if (count == 0) {
            ResultCode code = checkPcFormat();
            if (code != ADF_SUCCESS) {
                result = code;
            }
        }

        trackResult->resultCode = result;

        while (trackResult->resultCode != ADF_STAND_BY);
    }
}

/* required by PRU compiler */
#pragma DATA_SECTION(pru_remoteproc_ResourceTable, ".resource_table")
#pragma RETAIN(pru_remoteproc_ResourceTable)
struct my_resource_table {
    struct resource_table base;
    uint32_t offset[1];
} pru_remoteproc_ResourceTable = { 1, 0, 0, 0, 0 };
