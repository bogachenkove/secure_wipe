#ifndef ANALYZER_H
#define ANALYZER_H

#include "common.h"
#include "device_io.h"

error_code_t analyzerInitResult(analysis_result_t *result);
void analyzerFreeResult(analysis_result_t *result);
error_code_t analyzerScanDevice(device_t *dev, analysis_result_t *result,
                                progress_callback_t progress);
error_code_t analyzerAddBadSector(analysis_result_t *result, uint64_t sector);
bool analyzerIsBadSector(const analysis_result_t *result, uint64_t sector);
void analyzerPrintReport(const analysis_result_t *result);
uint64_t analyzerVerifyWipe(device_t *dev, progress_callback_t progress);

typedef enum {
    ANALYZE_READ_ONLY    = 0x00,
    ANALYZE_WRITE_TEST   = 0x01,
    ANALYZE_DETECT_WP    = 0x02
} analyze_flags_t;

typedef struct {
    analysis_result_t base;
    uint64_t *writeProtected;
    uint64_t  wpCount;
    uint64_t  wpCapacity;
    uint64_t  writeErrors;
    uint64_t  readErrors;
    uint64_t  verifyErrors;
    uint64_t  firstWpSector;
    uint64_t  lastWpSector;
    bool      hasWpRegions;
} extended_analysis_result_t;

error_code_t analyzerInitExtendedResult(extended_analysis_result_t *result);
void analyzerFreeExtendedResult(extended_analysis_result_t *result);
error_code_t analyzerScanDeviceExtended(device_t *dev,
                                        extended_analysis_result_t *result,
                                        analyze_flags_t flags,
                                        progress_callback_t progress);
void analyzerPrintExtendedReport(const extended_analysis_result_t *result);
bool analyzerQuickWpCheck(device_t *dev, uint64_t testSectors,
                          uint64_t *firstWpSector);

#endif