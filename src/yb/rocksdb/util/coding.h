//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Endian-neutral encoding:
// * Fixed-length numbers are encoded with least-significant byte first
// * In addition we support variable length "varint" encoding
// * Strings are encoded prefixed by their length in varint format

#pragma once

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "yb/rocksdb/port/port.h"

#include "yb/util/cast.h"
#include "yb/util/fast_varint.h"
#include "yb/util/slice_parts.h"

namespace rocksdb {

// Standard Put... routines append to a string
extern void PutFixed32(std::string* dst, uint32_t value);
extern void PutFixed64(std::string* dst, uint64_t value);
extern void PutVarint32(std::string* dst, uint32_t value);
extern void PutVarint64(std::string* dst, uint64_t value);
extern void PutLengthPrefixedSlice(std::string* dst, const Slice& value);
extern void PutLengthPrefixedSliceParts(std::string* dst,
                                        const SliceParts& slice_parts);

// Standard Get... routines parse a value from the beginning of a Slice
// and advance the slice past the parsed value.
extern bool GetFixed64(Slice* input, uint64_t* value);
bool GetVarint32(Slice* input, uint32_t* value);
extern bool GetVarint64(Slice* input, uint64_t* value);
extern bool GetLengthPrefixedSlice(Slice* input, Slice* result);
// This function assumes data is well-formed.
extern Slice GetLengthPrefixedSlice(const char* data);

extern Slice GetSliceUntil(Slice* slice, char delimiter);

// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// nullptr on error.  These routines only look at bytes in the range
// [p..limit-1]
extern const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* v);
extern const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* v);

// Might use effective performance optimization that reads before src, but not before
// read_allowed_from.
inline const char* GetSignedVarint64Ptr(const char* p, const char* limit, int64_t* value) {
  size_t decoded_size;
  if (!yb::FastDecodeSignedVarInt(p, limit - p, value, &decoded_size).ok()) {
    return nullptr;
  }
  return p + decoded_size;
}

// Returns the length of the varint32 or varint64 encoding of "v"
extern int VarintLength(uint64_t v);

// Lower-level versions of Put... that write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
extern void EncodeFixed32(char* dst, uint32_t value);
extern void EncodeFixed64(char* dst, uint64_t value);

// Lower-level versions of Put... that write directly into a character buffer
// and return a pointer just past the last byte written.
// REQUIRES: dst has enough space for the value being written
extern char* EncodeVarint32(char* dst, uint32_t value);
extern char* EncodeVarint64(char* dst, uint64_t value);

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint8_t DecodeFixed8(const char* ptr) {
  return pointer_cast<const uint8_t*>(ptr)[0];
}

inline uint32_t DecodeFixed32(const char* ptr) {
  if (port::kLittleEndian) {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0])))
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

inline uint32_t DecodeFixed32(const uint8_t* ptr) {
  return DecodeFixed32(reinterpret_cast<const char*>(ptr));
}

inline uint64_t DecodeFixed64(const char* ptr) {
  if (port::kLittleEndian) {
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
  }
}

inline uint64_t DecodeFixed64(const uint8_t* ptr) {
  return DecodeFixed64(reinterpret_cast<const char*>(ptr));
}

// Internal routine for use by fallback path of GetVarint32Ptr
extern const char* GetVarint32PtrFallback(const char* p,
                                          const char* limit,
                                          uint32_t* value);

const char* GetVarint64PtrFallback(const char* p, const char* limit, uint64_t* value);

inline const char* GetVarint32Ptr(const char* p,
                                  const char* limit,
                                  uint32_t* value) {
  if (p < limit) {
    uint32_t result = *(reinterpret_cast<const unsigned char*>(p));
    if ((result & 128) == 0) {
      *value = result;
      return p + 1;
    }
  }
  return GetVarint32PtrFallback(p, limit, value);
}

inline const char* GetVarint64Ptr(const char* p,
                                  const char* limit,
                                  uint64_t* value) {
  if (p < limit) {
    uint64_t result = *(reinterpret_cast<const unsigned char*>(p));
    if ((result & 128) == 0) {
      *value = result;
      return p + 1;
    }
  }
  return GetVarint64PtrFallback(p, limit, value);
}

// -- Implementation of the functions declared above
inline void EncodeFixed32(char* buf, uint32_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
  memcpy(buf, &value, sizeof(value));
#else
  buf[0] = value & 0xff;
  buf[1] = (value >> 8) & 0xff;
  buf[2] = (value >> 16) & 0xff;
  buf[3] = (value >> 24) & 0xff;
#endif
}

inline void EncodeFixed64(char* buf, uint64_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
  memcpy(buf, &value, sizeof(value));
#else
  buf[0] = value & 0xff;
  buf[1] = (value >> 8) & 0xff;
  buf[2] = (value >> 16) & 0xff;
  buf[3] = (value >> 24) & 0xff;
  buf[4] = (value >> 32) & 0xff;
  buf[5] = (value >> 40) & 0xff;
  buf[6] = (value >> 48) & 0xff;
  buf[7] = (value >> 56) & 0xff;
#endif
}

inline void PutFixed8(std::string* dst, uint8_t value) {
  dst->push_back(value);
}

inline void PutFixed32(std::string* dst, uint32_t value) {
  char buf[sizeof(value)];
  EncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

inline void PutFixed64(std::string* dst, uint64_t value) {
  char buf[sizeof(value)];
  EncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

inline void PutVarint32(std::string* dst, uint32_t v) {
  char buf[5];
  char* ptr = EncodeVarint32(buf, v);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

inline void PutSignedVarint(std::string* dst, int64_t v) {
  char buf[yb::kMaxVarIntBufferSize];
  size_t encoded_size;
  yb::FastEncodeSignedVarInt(v, pointer_cast<uint8_t*>(buf), &encoded_size);
  dst->append(buf, encoded_size);
}

inline char* EncodeVarint64(char* dst, uint64_t v) {
  static const unsigned int B = 128;
  unsigned char* ptr = reinterpret_cast<unsigned char*>(dst);
  while (v >= B) {
    *(ptr++) = (v & (B - 1)) | B;
    v >>= 7;
  }
  *(ptr++) = static_cast<unsigned char>(v);
  return reinterpret_cast<char*>(ptr);
}

inline char* FastEncodeVarint64(char* dst, uint64_t v) {
  if (v <= std::numeric_limits<uint32_t>::max()) {
    return EncodeVarint32(dst, static_cast<uint32_t>(v));
  }
  return EncodeVarint64(dst, v);
}

inline void PutVarint64(std::string* dst, uint64_t v) {
  char buf[10];
  char* ptr = EncodeVarint64(buf, v);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

inline void FastPutVarint64(std::string* dst, uint64_t v) {
  char buf[yb::kMaxVarIntBufferSize];
  char* ptr = FastEncodeVarint64(buf, v);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

inline void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
  PutVarint32(dst, static_cast<uint32_t>(value.size()));
  dst->append(value.cdata(), value.size());
}

inline void PutLengthPrefixedSliceParts(std::string* dst,
                                        const SliceParts& slice_parts) {
  size_t total_bytes = 0;
  for (int i = 0; i < slice_parts.num_parts; ++i) {
    total_bytes += slice_parts.parts[i].size();
  }
  PutVarint32(dst, static_cast<uint32_t>(total_bytes));
  for (int i = 0; i < slice_parts.num_parts; ++i) {
    dst->append(slice_parts.parts[i].cdata(), slice_parts.parts[i].size());
  }
}

inline int VarintLength(uint64_t v) {
  int len = 1;
  while (v >= 128) {
    v >>= 7;
    len++;
  }
  return len;
}

inline bool GetFixed64(Slice* input, uint64_t* value) {
  if (input->size() < sizeof(uint64_t)) {
    return false;
  }
  *value = DecodeFixed64(input->data());
  input->remove_prefix(sizeof(uint64_t));
  return true;
}

inline bool GetVarint32(Slice* input, uint32_t* value) {
  const char* p = input->cdata();
  const char* limit = p + input->size();
  const char* q = GetVarint32Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, static_cast<size_t>(limit - q));
    return true;
  }
}

inline bool GetVarint64(Slice* input, uint64_t* value) {
  const char* p = input->cdata();
  const char* limit = p + input->size();
  const char* q = GetVarint64Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, static_cast<size_t>(limit - q));
    return true;
  }
}

inline bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
  uint32_t len = 0;
  if (GetVarint32(input, &len) && input->size() >= len) {
    *result = Slice(input->data(), len);
    input->remove_prefix(len);
    return true;
  } else {
    return false;
  }
}

inline Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len = 0;
  // +5: we assume "data" is not corrupted
  auto p = GetVarint32Ptr(data, data + 5 /* limit */, &len);
  return Slice(p, len);
}

inline Slice GetSliceUntil(Slice* slice, char delimiter) {
  uint32_t len = 0;
  for (len = 0; len < slice->size() && slice->data()[len] != delimiter; ++len) {
    // nothing
  }

  Slice ret(slice->data(), len);
  slice->remove_prefix(len + ((len < slice->size()) ? 1 : 0));
  return ret;
}

}  // namespace rocksdb
