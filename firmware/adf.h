/*
ADF
Copyright (C) Dejan Ivkovic

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

#pragma once

#include <stdint.h>
#include <stddef.h>

#define ADF_MAX_SECTOR_COUNT 22
#define ADF_TRACK_RESULT_SECTOR_COUNT 11
#define ADF_HISTOGRAM_BUCKETS 2000
#define ADF_MAX_SECTOR_READ_ATTEMPTS ADF_MAX_SECTOR_COUNT * 4
#define ADF_HIGH_DENSITY_SECTORS_PER_TRACK 22
#define ADF_DOUBLE_DENSITY_SECTORS_PER_TRACK 11

#define ADF_AMIGA_SECTOR_FORMAT_IDENTIFIER 0xff
#define AMD_AMIGA_GAP_FILLER 0xaaaaaaaa
#define AMD_AMIGA_SECTOR_SYNC 0x44894489
#define AMD_AMIGA_EVEN_BIT_MASK 0x55555555

typedef enum {
    ADF_STAND_BY = 0,
    ADF_SUCCESS,
    ADF_CHECKSUM_ERROR,
    ADF_DATA_SIZE_ERROR,
    ADF_PC_DISK_DETECTED,
    ADF_RUNNING = 0xffffffff
} ResultCode;

typedef enum {
    ADF_DIAG_OK = 0,
    ADF_DIAG_SHORT_IMPULSE = 1,
    ADF_DIAG_LONG_IMPULSE = 2,
    ADF_DIAG_HEADER_CHECKSUM_ERROR = 4,
    ADF_DIAG_DATA_CHECKSUM_ERROR = 8
} Diagnostics;

typedef struct {
    uint8_t amigaFormat;
    uint8_t trackNumber;
    uint8_t sectorNumber;
    uint8_t sectorsUntilEndOfWritting;
} SectorInfo;

typedef struct {
    SectorInfo info;
    uint32_t label[4];
} SectorHeader;

typedef struct {
    SectorHeader header;
    uint32_t headerChecksum;
    uint32_t calculatedHeaderChecksum;
    uint32_t dataChecksum;
    uint32_t calculatedDataChecksum;
    uint8_t data[512];
} Sector;

typedef struct {
    uint32_t shortSignalTreshold;
    uint32_t singleZeroGapTreshold;
    uint32_t twoZerosGapTreshold;
    uint32_t threeZerosGapTreshold;
    uint32_t resultCode;
    uint32_t diagnostics;
    uint32_t readSectorMask;
    Sector sectors[ADF_TRACK_RESULT_SECTOR_COUNT];
    uint16_t signalHistogram[ADF_HISTOGRAM_BUCKETS];
} TrackResult;
