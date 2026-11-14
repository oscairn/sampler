// SPDX-License-Identifier: AGPL-3.0-or-later
// Provenance: the parameter list follows the reference's element and part parameters as listed in the
// research repository's docs/params (from the instrument's published parameter list); the ids are the
// sample-engine fork's oscairn_* opcode stems.
// The part (sound-wide) parameters, X(enum, id, low, high, default), as params.inc.
X(PartBendUp, "bend_up", 0, 24, 2)
X(PartBendDown, "bend_down", -48, 0, -2)
X(PartMono, "mono", 0, 1, 0)
X(PartPortamento, "portamento", 0, 127, 0)
X(PartVelDepth, "vel_depth", 0, 127, 64)     // the part's velocity sensitivity depth (64: as played)
X(PartVelOffset, "vel_offset", 0, 127, 64)   // and offset (64: none)
X(PartSplitOutputs, "split_outputs", 0, 1, 0) // 1: elements to their connection's output port (the converter sets it)
