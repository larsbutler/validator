/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Defines an instruction (decoder), based on the current location of
 * the instruction iterator. The instruction decoder takes a list
 * of candidate opcode (instruction) patterns, and searches for the
 * first candidate that matches the requirements of the opcode pattern.
 */

#include "native_client/src/trusted/validator/x86/decoder/nc_inst_state.h"

/* To turn on debugging of instruction decoding, change value of
 * DEBUGGING to 1.
 */
#define DEBUGGING 0

/*
 * This .c file contains includes, static functions, and constants that are used
 * in nc_inst_state.c, but have been factored out and put into this file, so
 * that we can test them. That is, to allow nc_inst_state.c and
 * nc_inst_state_Tests.cc to use them.
 */
#include "native_client/src/trusted/validator/x86/decoder/nc_inst_state_statics.c"

/* Returns true if the parsed instruction may be replaceable with a (hard coded)
 * opcode sequence. Used to prefer hard-coded opcode sequences over general
 * matches. These hard-coded opcode sequences are special nop opcode sequences,
 * most of which are generated by compilers and linkers. The need for these
 * hard-coded sequences is to accept nops with prefixes that would not otherwise
 * be accepted (such as "6666666666662e0f1f840000000000").
 */
static Bool NaClMaybeHardCodedNop(NaClInstState* state) {
  /* Note: This code is based on the hard-coded instructions
   * defined in function NaClDefNops in
   * src/trusted/validator_x86/ncdecode_tablegen.c.
   * If you change the hard-coded instructions defined in this
   * function, be sure that this predicate is updated accordingly to
   * return true for all such hard-coded instructions (it can
   * return true on other instructions without breaking the validator).
   */

  /* Check the first opcode byte to see if it is a special case. */
  switch (state->bytes.byte[state->num_prefix_bytes]) {
    case 0x90:
      /* If the first opcode byte is 90, it can be the NOP instruction.
       * Return true that this can be a nop.
       */
      if (state->num_opcode_bytes == 1) return TRUE;
      break;
    case 0x0f:
      /* Check that the second opcode byte is 1F or 0B. That is,
       * can the instruction be a NOP (0f1f) or the UD2 instruction.
       */
      if (state->num_opcode_bytes == 2) {
        uint8_t opcode_2 =
            state->bytes.byte[state->num_prefix_bytes + 1];
        if ((opcode_2 == 0x1f) || (opcode_2 == 0x0b)) return TRUE;
      }
      break;
    default:
      break;
  }
  /* If reached, can't be a (hard-coded) NOP instruction. */
  return FALSE;
}

/* Given the current location of the (relative) pc of the given instruction
 * iterator, update the given state to hold the (found) matched opcode
 * (instruction) pattern. If no matching pattern exists, set the state
 * to a matched undefined opcode (instruction) pattern. In all cases,
 * update the state to hold all information on the matched bytes of the
 * instruction. Note: The iterator expects that the opcode field is
 * changed from NULL to non-NULL by this fuction.
 */
void NaClDecodeInst(NaClInstIter* iter, NaClInstState* state) {
  uint8_t inst_length = 0;
  const NaClInst* cand_insts;
  Bool found_match = FALSE;

  /* Start by consuming the prefix bytes, and getting the possible
   * candidate opcode (instruction) patterns that can match, based
   * on the consumed opcode bytes.
   */
  NaClInstStateInit(iter, state);
  if (NaClConsumePrefixBytes(state)) {
    NaClInstPrefixDescriptor prefix_desc;
    Bool continue_loop = TRUE;
    NaClConsumeInstBytes(state, &prefix_desc);
    inst_length = state->bytes.length;
    while (continue_loop) {
      /* Try matching all possible candidates, in the order they are specified
       * (from the most specific prefix match, to the least specific prefix
       * match). Quit when the first pattern is matched.
       */
      if (prefix_desc.matched_prefix == NaClInstPrefixEnumSize) {
        continue_loop = FALSE;
      } else {
        uint8_t cur_opcode_prefix = prefix_desc.opcode_prefix;
        cand_insts = NaClGetNextInstCandidates(state, &prefix_desc,
                                               &inst_length);
        while (cand_insts != NULL) {
          NaClClearInstState(state, inst_length);
          state->inst = cand_insts;
          DEBUG(NaClLog(LOG_INFO, "try opcode pattern:\n"));
          DEBUG_OR_ERASE(NaClInstPrint(NaClLogGetGio(), state->decoder_tables,
                                       state->inst));
          if (NaClConsumeAndCheckOperandSize(state) &&
              NaClConsumeAndCheckAddressSize(state) &&
              NaClConsumeModRm(state) &&
              NaClConsumeSib(state) &&
              NaClConsumeDispBytes(state) &&
              NaClConsumeImmediateBytes(state) &&
              NaClValidatePrefixFlags(state)) {
            if (state->inst->flags & NACL_IFLAG(Opcode0F0F) &&
                /* Note: If all of the above code worked correctly,
                 * there should be no need for the following test.
                 * However, just to be safe, it is added.
                 */
                (state->num_imm_bytes == 1)) {
              /* Special 3DNOW instructions where opcode is in parsed
               * immediate byte at end of instruction. Look up in table,
               * and replace if found. Otherwise, let the default 0F0F lookup
               * act as the matching (invalid) instruction.
               */
              const NaClInst* cand_inst;
              uint8_t opcode_byte = state->bytes.byte[state->first_imm_byte];
              DEBUG(NaClLog(LOG_INFO,
                            "NaClConsume immediate byte opcode char: %"
                            NACL_PRIx8"\n", opcode_byte));
              cand_inst = NaClGetPrefixOpcodeInst(state->decoder_tables,
                                                  Prefix0F0F, opcode_byte);
              if (NULL != cand_inst) {
                state->inst = cand_inst;
                DEBUG(NaClLog(LOG_INFO, "Replace with 3DNOW opcode:\n"));
                DEBUG_OR_ERASE(NaClInstPrint(NaClLogGetGio(),
                                             state->decoder_tables,
                                             state->inst));
              }
            }

            /* found a match, exit loop. */
            found_match = TRUE;
            continue_loop = FALSE;
            state->num_opcode_bytes = inst_length - state->num_prefix_bytes;
            state->opcode_prefix = cur_opcode_prefix;

            /* If an instruction has both a general form, and a (hard-coded)
             * explicit opcode sequence, prefer the latter for the match.
             * Note: This selection to the (hard-coded) explicit opcode
             * sequence is necessary if we are to handle special nops with
             * multiple prefix bytes in the x86-64 validator.
             */
            if (NaClMaybeHardCodedNop(state)) {
              NaClConsumeHardCodedNop(state);
            }
            break;
          } else {
            /* match failed, try next candidate pattern. */
            cand_insts = NaClGetOpcodeInst(state->decoder_tables,
                                           cand_insts->next_rule);
          }
        }
        DEBUG(if (! found_match) {
            NaClLog(LOG_INFO, "no more candidates for this prefix\n");
          });
      }
    }
  }

  if (!found_match) {
    /* No instruction matched. Double check that it can't be a hard-coded
     * NOP instruction before we assume that we can't can accept the
     * instruction.
     */
    if (NaClConsumeHardCodedNop(state)) return;

    /* We did not match a defined opcode, match the undefined opcode,
     * forcing the inst field to be non-NULL, and to read at least one byte.
     */
    DEBUG(NaClLog(LOG_INFO, "no instruction found, converting to undefined\n"));
    state->inst = state->decoder_tables->undefined;
    if (state->bytes.length == 0 && state->bytes.length < state->length_limit) {
      /* Make sure we eat at least one byte. */
      NCInstBytesReadInline(&state->bytes);
    }
  }
}

const NaClInst* NaClInstStateInst(NaClInstState* state) {
  return state->inst;
}

NaClPcAddress NaClInstStatePrintableAddress(NaClInstState* state) {
  return state->iter->segment->vbase + state->inst_addr;
}

NaClExpVector* NaClInstStateExpVector(NaClInstState* state) {
  if (!state->nodes.is_defined) {
    state->nodes.is_defined = TRUE;
    NaClBuildExpVector(state);
  }
  return &state->nodes;
}

Bool NaClInstStateIsValid(NaClInstState* state) {
  return InstInvalid != state->inst->name;
}

uint8_t NaClInstStateLength(NaClInstState* state) {
  return state->bytes.length;
}

uint8_t NaClInstStateByte(NaClInstState* state, uint8_t index) {
  assert(index < state->bytes.length);
  return state->bytes.byte[index];
}

uint8_t NaClInstStateOperandSize(NaClInstState* state) {
  return state->operand_size;
}

uint8_t NaClInstStateAddressSize(NaClInstState* state) {
  return state->address_size;
}
