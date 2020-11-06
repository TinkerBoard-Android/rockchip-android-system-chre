/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chre/pal/util/wifi_scan_cache.h"

#include <inttypes.h>

#include "chre/util/macros.h"

/************************************************
 *  Prototypes
 ***********************************************/

struct chreWifiScanCacheState {
  //! true if the scan cache has started, i.e. chreWifiScanCacheScanEventBegin
  //! was invoked and has not yet ended.
  bool started;

  //! true if the current scan cache is a result of a CHRE active scan request.
  bool activeScanResult;

  //! The number of chreWifiScanResults dropped due to OOM.
  uint16_t numWifiScanResultsDropped;

  //! Stores the WiFi cache elements
  struct chreWifiScanEvent event;
  struct chreWifiScanResult resultList[CHRE_PAL_WIFI_SCAN_CACHE_CAPACITY];

  //! The number of chreWifiScanEvent data pending release via
  //! chreWifiScanCacheReleaseScanEvent().
  uint8_t numWifiEventsPendingRelease;

  uint32_t scannedFreqList[CHRE_WIFI_FREQUENCY_LIST_MAX_LEN];
};

/************************************************
 *  Global variables
 ***********************************************/
static const struct chrePalSystemApi *gSystemApi = NULL;
static const struct chrePalWifiCallbacks *gCallbacks = NULL;

static struct chreWifiScanCacheState gWifiCacheState;

static const uint64_t kOneMillisecondInNanoseconds = UINT64_C(1000000);

/************************************************
 *  Private functions
 ***********************************************/
static bool chreWifiScanCacheIsInitialized(void) {
  return (gSystemApi != NULL && gCallbacks != NULL);
}

static bool areAllScanEventsReleased(void) {
  return gWifiCacheState.numWifiEventsPendingRelease == 0;
}

/************************************************
 *  Public functions
 ***********************************************/
bool chreWifiScanCacheInit(const struct chrePalSystemApi *systemApi,
                           const struct chrePalWifiCallbacks *callbacks) {
  if (systemApi == NULL || callbacks == NULL) {
    return false;
  }

  gSystemApi = systemApi;
  gCallbacks = callbacks;
  memset(&gWifiCacheState, 0, sizeof(gWifiCacheState));

  return true;
}

void chreWifiScanCacheDeinit(void) {
  gSystemApi = NULL;
  gCallbacks = NULL;
}

bool chreWifiScanCacheScanEventBegin(enum chreWifiScanType scanType,
                                     uint8_t ssidSetSize,
                                     const uint32_t *scannedFreqList,
                                     uint16_t scannedFreqListLength,
                                     uint8_t radioChainPref,
                                     bool activeScanResult) {
  bool success = false;
  if (chreWifiScanCacheIsInitialized()) {
    if (gWifiCacheState.started) {
      gSystemApi->log(CHRE_LOG_ERROR, "Cannot start cache without ending");
    } else if (!areAllScanEventsReleased()) {
      gSystemApi->log(
          CHRE_LOG_ERROR,
          "Cannot start cache before releasing previous cache dispatch");
    } else {
      success = true;
      memset(&gWifiCacheState, 0, sizeof(gWifiCacheState));

      gWifiCacheState.event.version = CHRE_WIFI_SCAN_EVENT_VERSION;
      gWifiCacheState.event.scanType = scanType;
      gWifiCacheState.event.ssidSetSize = ssidSetSize;

      scannedFreqListLength =
          MIN(scannedFreqListLength, CHRE_WIFI_FREQUENCY_LIST_MAX_LEN);
      if (scannedFreqList != NULL) {
        memcpy(gWifiCacheState.scannedFreqList, scannedFreqList,
               scannedFreqListLength * sizeof(uint32_t));
      }
      gWifiCacheState.event.scannedFreqListLen = scannedFreqListLength;

      gWifiCacheState.event.radioChainPref = radioChainPref;
      gWifiCacheState.activeScanResult = activeScanResult;
      gWifiCacheState.started = true;
    }
  }

  if (activeScanResult && !success) {
    gCallbacks->scanResponseCallback(false /* pending */, CHRE_ERROR_BUSY);
  }

  return success;
}

void chreWifiScanCacheScanEventAdd(const struct chreWifiScanResult *result) {
  if (!areAllScanEventsReleased()) {
    gSystemApi->log(
        CHRE_LOG_ERROR,
        "Cannot use cache before releasing previous cache dispatch");
  } else {
    if (gWifiCacheState.event.resultTotal >=
        CHRE_PAL_WIFI_SCAN_CACHE_CAPACITY) {
      // TODO(b/172663268): Filter based on e.g. RSSI if full
      gWifiCacheState.numWifiScanResultsDropped++;
    } else {
      size_t index = gWifiCacheState.event.resultTotal;
      memcpy(&gWifiCacheState.resultList[index], result,
             sizeof(const struct chreWifiScanResult));

      // ageMs will be properly populated in chreWifiScanCacheScanEventEnd
      gWifiCacheState.resultList[index].ageMs = (uint32_t)(
          gSystemApi->getCurrentTime() / kOneMillisecondInNanoseconds);

      gWifiCacheState.event.resultTotal++;
    }
  }
}

void chreWifiScanCacheScanEventEnd(enum chreError errorCode) {
  if (gWifiCacheState.started) {
    if (gWifiCacheState.numWifiScanResultsDropped > 0) {
      gSystemApi->log(CHRE_LOG_WARN,
                      "Dropped total of %" PRIu32 " access points",
                      gWifiCacheState.numWifiScanResultsDropped);
    }
    if (gWifiCacheState.activeScanResult) {
      gCallbacks->scanResponseCallback(
          errorCode == CHRE_ERROR_NONE /* pending */, errorCode);
    }

    if (errorCode == CHRE_ERROR_NONE) {
      gWifiCacheState.event.referenceTime = gSystemApi->getCurrentTime();
      gWifiCacheState.event.scannedFreqList = gWifiCacheState.scannedFreqList;

      uint32_t referenceTimeMs = (uint32_t)(
          gWifiCacheState.event.referenceTime / kOneMillisecondInNanoseconds);

      uint8_t eventIndex = 0;
      for (uint16_t i = 0; i < gWifiCacheState.event.resultTotal;
           i += CHRE_PAL_WIFI_SCAN_CACHE_MAX_RESULT_COUNT) {
        gWifiCacheState.event.resultCount =
            MIN(CHRE_PAL_WIFI_SCAN_CACHE_MAX_RESULT_COUNT,
                (uint8_t)(gWifiCacheState.event.resultTotal - i));
        gWifiCacheState.event.eventIndex = eventIndex++;
        gWifiCacheState.event.results = &gWifiCacheState.resultList[i];
        for (uint8_t j = 0; j < gWifiCacheState.event.resultCount; j++) {
          gWifiCacheState.resultList[i + j].ageMs =
              referenceTimeMs - gWifiCacheState.resultList[i + j].ageMs;
        }

        // TODO: The current approach only works for situations where the
        // event is released immediately. Add a way to handle this scenario
        // (e.g. an array of chreWifiScanEvent's).
        gWifiCacheState.numWifiEventsPendingRelease++;
        gCallbacks->scanEventCallback(&gWifiCacheState.event);
      }
    }

    gWifiCacheState.started = false;
    gWifiCacheState.activeScanResult = false;
  }
}

bool chreWifiScanCacheDispatchFromCache(
    const struct chreWifiScanParams *params) {
  if (!chreWifiScanCacheIsInitialized()) {
    return false;
  }

  UNUSED_VAR(params);
  // TODO(b/172663268): Implement this
  return false;
}

void chreWifiScanCacheReleaseScanEvent(struct chreWifiScanEvent *event) {
  if (!chreWifiScanCacheIsInitialized()) {
    return;
  }

  if (event != &gWifiCacheState.event) {
    gSystemApi->log(CHRE_LOG_ERROR, "Invalid event pointer %p", event);
  } else if (gWifiCacheState.numWifiEventsPendingRelease > 0) {
    gWifiCacheState.numWifiEventsPendingRelease--;
  }
}

void chreWifiScanCacheConfigureScanMonitor(bool enable) {
  if (!chreWifiScanCacheIsInitialized()) {
    return;
  }

  UNUSED_VAR(enable);
  // TODO(b/172663268): Implement this
}