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

#include "chpp/services/discovery.h"
#include "chpp/services.h"

/************************************************
 *  Prototypes
 ***********************************************/

static void chppDiscoveryDiscoverAll(struct ChppAppState *context,
                                     const struct ChppAppHeader *rxHeader);

/************************************************
 *  Private Functions
 ***********************************************/

/**
 * Processes the Discover All Services (0x0001) request.
 *
 * @param context Maintains status for each app layer instance.
 * @param requestHeader Request datagram header. Cannot be null.
 */
static void chppDiscoveryDiscoverAll(
    struct ChppAppState *context, const struct ChppAppHeader *requestHeader) {
  // Allocate response
  size_t responseLen =
      sizeof(struct ChppAppHeader) +
      context->registeredServiceCount * sizeof(struct ChppServiceDescriptor);

  struct ChppDiscoveryResponse *response = chppAllocServiceResponseTypedArray(
      requestHeader, struct ChppDiscoveryResponse,
      context->registeredServiceCount, services);

  if (response == NULL) {
    LOG_OOM("DiscoverAll response of %zu bytes", responseLen);
    CHPP_ASSERT(false);

  } else {
    // Populate list of services
    for (size_t i = 0; i < context->registeredServiceCount; i++) {
      response->services[i] = context->registeredServices[i]->descriptor;
    }

    // Send out response datagram
    chppEnqueueTxDatagramOrFail(context->transportContext, response,
                                responseLen);
  }
}

/************************************************
 *  Public Functions
 ***********************************************/

void chppDispatchDiscoveryClientRequest(struct ChppAppState *context,
                                        const uint8_t *buf, size_t len) {
  UNUSED_VAR(len);
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  switch (rxHeader->command) {
    case CHPP_DISCOVERY_COMMAND_DISCOVER_ALL: {
      // Send back a list of services supported by this platform.
      chppDiscoveryDiscoverAll(context, rxHeader);
      break;
    }
    default: {
      LOGE(
          "Received unknown discovery command: %#x, transaction ID = "
          "%" PRIu8,
          rxHeader->command, rxHeader->transaction);
      break;
    }
  }
}