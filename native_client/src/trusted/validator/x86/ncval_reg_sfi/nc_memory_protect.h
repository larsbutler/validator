/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_X86_NCVAL_REG_SFI_NC_MEMORY_PROTECT_H__
#define NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_X86_NCVAL_REG_SFI_NC_MEMORY_PROTECT_H__

/* nc_memory_protect.h - For 64-bit mode, verifies that we don't access
 * memory that is out of range.
 */

#include "native_client/src/shared/utils/types.h"
#include "native_client/src/trusted/validator/x86/decoder/gen/ncopcode_operand_kind.h"

struct NaClValidatorState;

/*
 * When true, check both uses and sets of memory. When false, only
 * check sets.
 */
extern Bool NACL_FLAGS_read_sandbox;

/*
 * Verifies that we don't access memory store with an out of range
 * address. That implies that when storing (or reading when doing read
 * sandboxing), in a memory offset of the form:
 *
 *     [base + index * scale + displacement]
 *
 * (1) base is either the reserved base register (r15), or rsp, or rbp.
 *
 * (2) Either the index isn't defined, or the index is a 32-bit register and
 *     the previous instruction must assign a value to index that is 32-bits
 *     with zero extension.
 *
 * (3) The displacement can't be larger than 32 bits.
 *
 * (4) Segment addresses s:b where s is in {CS, DS, ES, SS} and
 *     b is a safe address generated by a LEA instruction.
 *
 * SPECIAL CASE: We allow all stores of the form [%rip + displacement].
 *
 * NOTE: in x86 code, displacements can't be larger than 32 bits.
 */
void NaClMemoryReferenceValidator(struct NaClValidatorState* state);

#ifdef NCVAL_TESTING
/* Returns true if the current instruction is an LEA instruction that
 * generates a safe memory address.
 */
Bool NaClAcceptLeaSafeAddress(struct NaClValidatorState* state);
#endif

#endif  /* NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_X86_NCVAL_REG_SFI_NC_MEMORY_PROTECT_H__ */
