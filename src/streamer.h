// SPDX-License-Identifier: AGPL-3.0-or-later
// The process's one I/O thread ("osc-sampler-io", normal scheduling, never real-time), shared by every
// instance: it decodes ahead into each streaming voice's ring (pcm::Stream, by the rules in core/pcore.h),
// most urgent first, and takes released rings back. The audio thread never waits for it and never takes a
// lock it holds; a voice whose next frames come too late is shed by the core (docs/design.md, "Streaming").
#pragma once
#include <mutex>

#include "../core/pcore.h"
#include "packs.h"

namespace pcmshell {

// What the I/O thread plays for one region (a Stream's source): region frame f is frame start + f of the
// record. Stream frame 0 is region frame `head`; the frames run to `stop`, then kGuardPost zero frames when
// `tail` (a one-shot's end), or back to loopStart at loopEnd when `loop` (a loop too long to keep).
struct StreamSource {
    Record record;
    uint32_t start = 0, head = 0, stop = 0, loopStart = 0, loopEnd = 0;
    bool loop = false, tail = false;
};

// An instance's streams join and leave the thread (main thread). After detach returns the thread no longer
// touches them. The thread starts with the first instance and stops with the last.
void attachStreams(pcm::Stream* streams, uint32_t count);
void detachStreams(pcm::Stream* streams);
// A stream became Active (the audio thread, through Core::onStreamStart): wakes the thread if it sleeps. Never waits,
// locks or allocates.
void wakeStreams();
// Held while freeing what streams read (their sources, records and packs): the thread is between units.
std::unique_lock<std::mutex> holdStreams();
// Tests: stall the thread (it decodes nothing; released rings still come back), wait until it has
// filled every active ring (main thread), and its passes that found nothing to do.
void stallStreams(bool on);
void settleStreams();
uint64_t idleStreamPasses();

}  // namespace pcmshell
