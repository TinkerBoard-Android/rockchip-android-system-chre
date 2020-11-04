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

#include "chre/platform/shared/log_buffer.h"
#include "chre/util/lock_guard.h"

#include <cstdarg>
#include <cstdio>

namespace chre {

LogBuffer::LogBuffer(LogBufferCallbackInterface *callback, void *buffer,
                     size_t bufferSize)
    : mBufferData(static_cast<uint8_t *>(buffer)),
      mBufferMaxSize(bufferSize),
      mCallback(callback) {
  CHRE_ASSERT(bufferSize >= kBufferMinSize);
}

void LogBuffer::handleLog(LogBufferLogLevel logLevel, uint32_t timestampMs,
                          const char *log) {
  constexpr size_t maxLogLen = kLogMaxSize - kLogDataOffset;
  uint8_t logLen = static_cast<uint8_t>(strnlen(log, maxLogLen));
  if (logLen == maxLogLen) {
    // So that the last character is not copied to buffer and a null terminator
    // can be copied there instead.
    logLen--;
  }
  size_t totalLogSize = kLogDataOffset + logLen;

  if (totalLogSize > mBufferMaxSize) {
    return;
  }

  {
    LockGuard<Mutex> lockGuard(mBufferDataLock);
    // Invalidate memory allocated for log at head while the buffer is greater
    // than max size
    while (getBufferSize() + totalLogSize > mBufferMaxSize) {
      size_t logSize;
      mBufferDataHeadIndex = getNextLogIndex(mBufferDataHeadIndex, &logSize);
      mBufferDataSize -= logSize;
    }
    copyToBuffer(sizeof(logLevel), &logLevel);
    copyToBuffer(sizeof(timestampMs), &timestampMs);
    copyToBuffer(logLen, reinterpret_cast<const void *>(log));
    copyToBuffer(1, reinterpret_cast<const void *>("\0"));
  }

  switch (mNotificationSetting) {
    case LogBufferNotificationSetting::ALWAYS: {
      mCallback->onLogsReady(this);
      break;
    }
    case LogBufferNotificationSetting::NEVER: {
      break;
    }
    case LogBufferNotificationSetting::THRESHOLD: {
      if (getBufferSize() > mNotificationThresholdBytes) {
        mCallback->onLogsReady(this);
      }
      break;
    }
  }
}

size_t LogBuffer::copyLogs(void *destination, size_t size) {
  LockGuard<Mutex> lock(mBufferDataLock);

  size_t copySize = 0;

  if (size != 0 && destination != nullptr && getBufferSize() != 0) {
    size_t logStartIndex = mBufferDataHeadIndex;
    size_t logDataStartIndex =
        incrementAndModByBufferMaxSize(logStartIndex, kLogDataOffset);
    // There is guaranteed to be a null terminator within the max log length
    // number of bytes
    size_t logDataSize = getLogDataLength(logDataStartIndex);
    size_t logSize = kLogDataOffset + logDataSize;
    size_t sizeAfterAddingLog = logSize;
    while (sizeAfterAddingLog <= size &&
           sizeAfterAddingLog <= getBufferSize()) {
      logStartIndex = getNextLogIndex(logStartIndex, &logSize);
      sizeAfterAddingLog += logSize;
    }
    copySize = sizeAfterAddingLog - logSize;
    copyFromBuffer(copySize, destination);
    mBufferDataHeadIndex = logStartIndex;
  }

  return copySize;
}

void LogBuffer::transferTo(LogBuffer & /*buffer*/) {
  // TODO(b/146164384): Implement
}

void LogBuffer::updateNotificationSetting(LogBufferNotificationSetting setting,
                                          size_t thresholdBytes) {
  LockGuard<Mutex> lock(mBufferDataLock);

  mNotificationSetting = setting;
  mNotificationThresholdBytes = thresholdBytes;
}

size_t LogBuffer::getBufferSize() const {
  return mBufferDataSize;
}

size_t LogBuffer::incrementAndModByBufferMaxSize(size_t originalVal,
                                                 size_t incrementBy) const {
  return (originalVal + incrementBy) % mBufferMaxSize;
}

void LogBuffer::copyToBuffer(size_t size, const void *source) {
  const uint8_t *sourceBytes = static_cast<const uint8_t *>(source);
  if (mBufferDataTailIndex + size > mBufferMaxSize) {
    size_t firstSize = mBufferMaxSize - mBufferDataTailIndex;
    size_t secondSize = size - firstSize;
    memcpy(&mBufferData[mBufferDataTailIndex], sourceBytes, firstSize);
    memcpy(mBufferData, &sourceBytes[firstSize], secondSize);
  } else {
    memcpy(&mBufferData[mBufferDataTailIndex], sourceBytes, size);
  }
  mBufferDataSize += size;
  mBufferDataTailIndex =
      incrementAndModByBufferMaxSize(mBufferDataTailIndex, size);
}

void LogBuffer::copyFromBuffer(size_t size, void *destination) {
  uint8_t *destinationBytes = static_cast<uint8_t *>(destination);
  if (mBufferDataHeadIndex + size > mBufferMaxSize) {
    size_t firstSize = mBufferMaxSize - mBufferDataHeadIndex;
    size_t secondSize = size - firstSize;
    memcpy(destinationBytes, &mBufferData[mBufferDataHeadIndex], firstSize);
    memcpy(&destinationBytes[firstSize], mBufferData, secondSize);
  } else {
    memcpy(destinationBytes, &mBufferData[mBufferDataHeadIndex], size);
  }
  mBufferDataSize -= size;
  mBufferDataHeadIndex =
      incrementAndModByBufferMaxSize(mBufferDataHeadIndex, size);
}

size_t LogBuffer::getNextLogIndex(size_t startingIndex, size_t *logSize) {
  size_t logDataStartIndex =
      incrementAndModByBufferMaxSize(startingIndex, kLogDataOffset);

  size_t logDataSize = getLogDataLength(logDataStartIndex);
  *logSize = kLogDataOffset + logDataSize;
  return incrementAndModByBufferMaxSize(startingIndex, *logSize);
}

size_t LogBuffer::getLogDataLength(size_t startingIndex) {
  size_t currentIndex = startingIndex;
  constexpr size_t maxBytes = kLogMaxSize - kLogDataOffset;
  size_t numBytes = maxBytes + 1;

  for (size_t i = 0; i < maxBytes; i++) {
    if (mBufferData[currentIndex] == '\0') {
      // +1 to include the null terminator
      numBytes = i + 1;
      break;
    }
    currentIndex = incrementAndModByBufferMaxSize(currentIndex, 1);
  }
  return numBytes;
}

}  // namespace chre