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

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>

#include <rc/pru.h>

#include "firmware/adf.h"

using namespace std::chrono_literals;

struct GpioIn {
    int gpio;
};

struct GpioOut {
    int gpio;
};

constexpr const GpioIn GPIO_IN_TRACK_0 { 74 };
constexpr const GpioIn GPIO_IN_DISK_READY { 70 };

constexpr const GpioOut GPIO_OUT_MOTOR_ENABLE { 77 };
constexpr const GpioOut GPIO_OUT_DIRECTION { 75 };
constexpr const GpioOut GPIO_OUT_STEP { 73 };
constexpr const GpioOut GPIO_OUT_HEAD_SELECT { 71 };
constexpr const GpioOut GPIO_OUT_DRIVE_SELECT { 72 };

constexpr const int DISK_GEOMETRY_TRACKS { 80 };
constexpr const int DISK_GEOMETRY_HEADS { 2 };

constexpr const uint32_t THRESHOLD_HD_SHORT_GAP = 200;
constexpr const uint32_t THRESHOLD_HD_SINGLE_ZERO_GAP = 500;
constexpr const uint32_t THRESHOLD_HD_DOUBLE_ZERO_GAP = 700;
constexpr const uint32_t THRESHOLD_HD_TRIPLE_ZERO_GAP = 900;

constexpr const uint32_t THRESHOLD_DD_SHORT_GAP = 600;
constexpr const uint32_t THRESHOLD_DD_SINGLE_ZERO_GAP = 1000;
constexpr const uint32_t THRESHOLD_DD_DOUBLE_ZERO_GAP = 1400;
constexpr const uint32_t THRESHOLD_DD_TRIPLE_ZERO_GAP = 1800;


constexpr const char* GPIO_BASE_NAME = "/sys/class/gpio/gpio";

volatile std::sig_atomic_t gSignalStatus;

void writeGpioSetting(const int gpio, const std::string_view setting, const std::string_view val) {
        std::ostringstream fileName;
        fileName << GPIO_BASE_NAME << gpio << "/" << setting;
        std::ofstream file{ fileName.str() };
        file.exceptions(std::ofstream::badbit);
        file << val;
}

void configurePins() {
    constexpr auto PIN_DIRECTION="direction";
    constexpr auto PIN_DIRECTION_IN="in";
    constexpr auto PIN_DIRECTION_OUT="out";
    constexpr auto PIN_ACTIVE_LOW="active_low";
    constexpr auto PIN_ACTIVE_LOW_TRUE="1";

    constexpr std::array<GpioIn, 2> inputPins 
        { GPIO_IN_TRACK_0, GPIO_IN_DISK_READY };
    constexpr std::array<GpioOut, 5> outputPins
        { GPIO_OUT_MOTOR_ENABLE, GPIO_OUT_DIRECTION, GPIO_OUT_STEP,
          GPIO_OUT_HEAD_SELECT, GPIO_OUT_DRIVE_SELECT };

    for (const auto gpio : inputPins) {
        writeGpioSetting(gpio.gpio, PIN_DIRECTION, PIN_DIRECTION_IN);
        writeGpioSetting(gpio.gpio, PIN_ACTIVE_LOW, PIN_ACTIVE_LOW_TRUE);
    }

    for (const auto gpio : outputPins) {
        writeGpioSetting(gpio.gpio, PIN_DIRECTION, PIN_DIRECTION_OUT);
        writeGpioSetting(gpio.gpio, PIN_ACTIVE_LOW, PIN_ACTIVE_LOW_TRUE);
    }
}

void setValue(GpioOut gpio, bool value) {
    std::ostringstream fileName;
    fileName << GPIO_BASE_NAME << gpio.gpio << "/value";
    std::ofstream file{ fileName.str() };
    file.exceptions(std::ofstream::badbit);
    file << (value ? "1" : "0");
} 

bool getValue(GpioIn gpio) {
    std::ostringstream fileName;
    fileName << GPIO_BASE_NAME << gpio.gpio << "/value";
    std::ifstream file{ fileName.str() };
    file.exceptions(std::ofstream::badbit);
    std::string value;
    file >> value;
    return value == "1";
}

void stepHead() {
    setValue(GPIO_OUT_STEP, 1);
    std::this_thread::sleep_for(10ms);
    setValue(GPIO_OUT_STEP, 0);
    std::this_thread::sleep_for(10ms);
    setValue(GPIO_OUT_STEP, 1);
    std::this_thread::sleep_for(10ms);
}

void stepHeadForwards() {
    setValue(GPIO_OUT_DIRECTION, 1);
    std::this_thread::sleep_for(10ms);
    stepHead();
}

void stepHeadBackwards() {
    setValue(GPIO_OUT_DIRECTION, 0);
    std::this_thread::sleep_for(10ms);
    stepHead();
}

void moveToTrack0() {
    int maxTracks = DISK_GEOMETRY_TRACKS + 10; // arbitrary offset to make sure we get to track 0
    while (!getValue(GPIO_IN_TRACK_0) && maxTracks) {
        stepHeadBackwards();
        --maxTracks;
    }
    if (!getValue(GPIO_IN_TRACK_0)) {
        std::cerr << "Could not move to track 0." << std::endl;
    }
}

void selectDrive(bool isSelected) {
    setValue(GPIO_OUT_DRIVE_SELECT, isSelected ? 1 : 0);
}

std::string getStatusString(ResultCode code) 
{
    std::string result;
    switch(code) {
    case ADF_STAND_BY:
        result = "stand by";
        break;
    case ADF_SUCCESS:
        result = "success";
        break;
    case ADF_CHECKSUM_ERROR:
        result = "checksum error";
        break;
    case ADF_DATA_SIZE_ERROR:
        result = "data size error";
        break;
    case ADF_PC_DISK_DETECTED:
        result = "pc format detected";
        break;
    case ADF_RUNNING:
        result = "running";
        break;
    default:
        result = "unknown";
    }

    return result;
}

std::string getDiagnosticsString(Diagnostics diags) {
    std::string result;
    if (diags == ADF_DIAG_OK) {
        result = "OK";
    }
    else {
        if (diags &  ADF_DIAG_SHORT_IMPULSE) {
            result += "[short impulse detected]";
        }
        if (diags &  ADF_DIAG_LONG_IMPULSE) {
            result += "[long impulse detected]";
        }
        if (diags &  ADF_DIAG_HEADER_CHECKSUM_ERROR) {
            result += "[header checksum serror]";
        }
        if (diags &  ADF_DIAG_DATA_CHECKSUM_ERROR) {
            result += "[data checksum serror]";
        }
    }
    return result;
}

bool readTrack(volatile TrackResult* trackResult, bool isHighDensityDisk, uint32_t sectorMask, bool ignoreErrors = false)
{
    bool errorDetected { false };

    // restart PRU for each track to ensure cycle counter (used for time measurment) resets
    rc_pru_stop(0);

    memset((void*)trackResult, 0, sizeof(*trackResult));
    if (rc_pru_start(0, "am335x-pru0-adf-fw")) {
        std::cerr << "Could not start PRU firware." << std::endl;
        return false;
    }

    if (isHighDensityDisk) {
        trackResult->shortSignalTreshold = THRESHOLD_HD_SHORT_GAP;
        trackResult->singleZeroGapTreshold = THRESHOLD_HD_SINGLE_ZERO_GAP;
        trackResult->twoZerosGapTreshold = THRESHOLD_HD_DOUBLE_ZERO_GAP;
        trackResult->threeZerosGapTreshold = THRESHOLD_HD_TRIPLE_ZERO_GAP;
    } else {
        trackResult->shortSignalTreshold = THRESHOLD_DD_SHORT_GAP;
        trackResult->singleZeroGapTreshold = THRESHOLD_DD_SINGLE_ZERO_GAP;
        trackResult->twoZerosGapTreshold = THRESHOLD_DD_DOUBLE_ZERO_GAP;
        trackResult->threeZerosGapTreshold = THRESHOLD_DD_TRIPLE_ZERO_GAP;
    }

    trackResult->readSectorMask = sectorMask;

    // PRU go!
    trackResult->resultCode = ADF_RUNNING;
    

    int count = 0;
    while (trackResult->resultCode == ADF_RUNNING && count < 20 && !gSignalStatus) {
        std::this_thread::sleep_for(100ms);
        count++;
    }

    if (count == 20 && !ignoreErrors) {
        std::cerr 
            << "Timeout while looking for sync. " 
            << " Diagnostics: " << getDiagnosticsString(static_cast<Diagnostics>(trackResult->diagnostics)) << std::endl;
        errorDetected = true;
        
    } else if (trackResult->resultCode != ADF_SUCCESS) {
        errorDetected = true;
    }

    return !errorDetected;
}

bool verifyTrack(volatile TrackResult* trackResult) 
{
    bool errorDetected = false;
    for (const auto& sector : trackResult->sectors) {
        if (sector.header.info.amigaFormat != ADF_AMIGA_SECTOR_FORMAT_IDENTIFIER) {
            std::cerr << "Error: no amiga format." << std::endl;
            errorDetected = true;
            break;
        }
    }
    return !errorDetected;
}

void writeTrackToFile(const std::vector<std::optional<Sector>> &sectors, std::ofstream &file, bool isHighDensityDisk) {
	const int sectorCount = isHighDensityDisk ? ADF_HIGH_DENSITY_SECTORS_PER_TRACK : ADF_DOUBLE_DENSITY_SECTORS_PER_TRACK;
    for (int sectorNum = 0; sectorNum < sectorCount; ++sectorNum) {
        bool sectorFound{ false };
        for (const auto& sector : sectors) {
            if (sector && sector->header.info.sectorNumber == sectorNum) {
                file.write(const_cast<const char*>(reinterpret_cast<volatile const char*>(sector->data)), sizeof(sector->data));
                sectorFound = true;
                if (sector->headerChecksum != sector->calculatedHeaderChecksum) {
                   std::cerr << "Sector " << sectorNum << " has header checksum error." << std::endl;
                }
                if (sector->dataChecksum != sector->calculatedDataChecksum) {
                   std::cerr << "Sector " << sectorNum << " has data checksum error." << std::endl;            
                }
            }
        }
        if (!sectorFound) {
            std::cerr << "Sector " << sectorNum << " was not found. Filling with zeros instead" << std::endl;
            for (int k = 0; k < 512; ++k) {
                file.put(0);
            }
        }
    }
}

void signalHandler(int signal)
{
    gSignalStatus = signal;
}

int main() {
	// 12000 bytes is the size of shared memory in PRU
    static_assert(sizeof(TrackResult) < 12000);

    // install signal handler
    std::signal(SIGINT, signalHandler);

    rc_pru_stop(0);

    auto trackResult = reinterpret_cast<volatile TrackResult*>(rc_pru_shared_mem_ptr());
    if (trackResult == nullptr) {
        std::cerr << "Could not locate PRU shared memory." << std::endl;
        return 1;
    }

    configurePins();

    // activate drive
    selectDrive(true); 

    std::cout << "Moving to track 0...\r" << std::flush;
    moveToTrack0();

	trackResult->shortSignalTreshold = THRESHOLD_DD_SHORT_GAP;
	trackResult->singleZeroGapTreshold = THRESHOLD_DD_SINGLE_ZERO_GAP;
	trackResult->twoZerosGapTreshold = THRESHOLD_DD_DOUBLE_ZERO_GAP;
	trackResult->threeZerosGapTreshold = THRESHOLD_DD_TRIPLE_ZERO_GAP;

	// we need to read the track to detect density
    readTrack(trackResult, false, 0, true);

    uint16_t sum{};
    for (int i = 0; i < THRESHOLD_HD_SINGLE_ZERO_GAP; ++i) {
        sum += trackResult->signalHistogram[i];
    }
	
	constexpr const uint16_t HIST_MIN_SUM_UNDER_HD_SINGLE_ZERO_PEAK { 1000 };
    const bool isHighDensityDisk {sum > HIST_MIN_SUM_UNDER_HD_SINGLE_ZERO_PEAK};

    if (isHighDensityDisk) {
        std::cout << "Reading High Density (HD) Disc."  << std::endl;
    } else {
        std::cout << "Reading Double Density (DD) Disc. " << std::endl;
    }

    setValue(GPIO_OUT_DIRECTION, 1);
    setValue(GPIO_OUT_MOTOR_ENABLE, 1);
    std::ofstream file("out.adf", std::ios::binary);
    bool success = false;
    bool pcDiskDetected = false;
    for (int track = 0; track < DISK_GEOMETRY_TRACKS && !gSignalStatus && !pcDiskDetected; ++track) {
        uint32_t diagnostics{};
        for (int head = 0; head < DISK_GEOMETRY_HEADS && !gSignalStatus && !pcDiskDetected; ++head) {
            std::vector<std::optional<Sector>> sectors;
            sectors.resize(isHighDensityDisk ? ADF_HIGH_DENSITY_SECTORS_PER_TRACK : ADF_DOUBLE_DENSITY_SECTORS_PER_TRACK);

            std::cout << std::string(80, ' ')  << "\r" << std::flush;
            std::cout << "Track: " << track << " head: " << head << " \r" << std::flush;
            setValue(GPIO_OUT_HEAD_SELECT, head);

            success = false;

            constexpr const int maxRetryAttempts = 50;
            constexpr const int moveHeadEveryAttempts = 5;

            for (int attempt = 0; attempt < maxRetryAttempts && !success && !gSignalStatus && !pcDiskDetected; ++attempt) {
                success = readTrack(trackResult, isHighDensityDisk, 0) && verifyTrack(trackResult);
                
				if (isHighDensityDisk) {
					success = readTrack(trackResult, isHighDensityDisk, trackResult->readSectorMask) && verifyTrack(trackResult) && success;
				}

				// write signal histogram to a file for debugging/analysis purpose
                std::ofstream f("hist.txt");
                for (int i = 0; i < ADF_HISTOGRAM_BUCKETS; ++i) {
                    f << trackResult->signalHistogram[i] << ", ";
                }
                f.close();

                // keep sectors with valid checksums
                for (const auto& sector : trackResult->sectors) {
                    if (sector.header.info.sectorNumber < ADF_MAX_SECTOR_COUNT) {
                        auto& cachedSector = sectors[sector.header.info.sectorNumber];
                        if (cachedSector) {
                            if (cachedSector->headerChecksum != cachedSector->calculatedHeaderChecksum || cachedSector->dataChecksum != cachedSector->calculatedDataChecksum) {
                                cachedSector = const_cast<const Sector&>(sector);
                            }
                        } else {
                            cachedSector = const_cast<const Sector&>(sector);
                        }
                    }
                }

                // count valid sectors
                auto countValid = std::count_if(sectors.cbegin(), sectors.cend(), [](const auto& sector) -> bool {
                    return sector && sector->headerChecksum == sector->calculatedHeaderChecksum && sector->dataChecksum == sector->calculatedDataChecksum;
                });
                if (countValid == sectors.size()) {
                    success = true;
                    break;
                }

                if (!success) {
                    pcDiskDetected = (trackResult->resultCode == ADF_PC_DISK_DETECTED);
                    diagnostics |= trackResult->diagnostics;
                    std::cout << "Status: " << getStatusString(static_cast<ResultCode>(trackResult->resultCode)) <<". Retrying (" << attempt + 1 << "/" << maxRetryAttempts << ")... \r" << std::flush;

                    // every few attempts step the head back and forward one track
                    if ((attempt + 1) % moveHeadEveryAttempts == 0) {
                        if (track > 0) {
                            stepHeadBackwards();
                            std::this_thread::sleep_for(200ms);
                            stepHeadForwards();
                        } else {
                            stepHeadForwards();
                            std::this_thread::sleep_for(200ms);
                            stepHeadBackwards();
                        }
		            }
                }
            }

            if (!success) {
                std::cout << std::string(80, ' ')  << "\r" << std::flush;
                std::cerr << "Track " << track << " has errors. Diagnostics: " << getDiagnosticsString(static_cast<Diagnostics>(diagnostics)) << std::endl;
            }

            if (!pcDiskDetected) {
                writeTrackToFile(sectors, file, isHighDensityDisk);
            }

            if (head == DISK_GEOMETRY_HEADS - 1) {
                stepHeadForwards();
            }
        }
    }

    std::cout << std::string(80, ' ')  << "\r" << std::flush;
    if (gSignalStatus) {
    	std::cout << "Interrupted." << std::endl;

    } else if (pcDiskDetected) {
        std::cerr << "PC disk detected. Aborting." << std::endl;
    } else {
	    std::cout << "Done." << std::endl;
    }

    rc_pru_stop(0);

    setValue(GPIO_OUT_MOTOR_ENABLE, 0);
    selectDrive(false); 

    return 0;
}
