/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

uint64_t OMInitCompatibleAccelIKJ(uint64_t version) {
  return version == 0x000100;
}
