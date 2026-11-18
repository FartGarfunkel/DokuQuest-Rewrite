// client/bench.h — DQ_BENCH=<file.csv>: one line per frame with the CPU time of each part of the frame loop (input,
// update, the world's passes, the menus, the swap, the wait for the GPU, the limiter's sleep), the GPU's own time for
// the frame (OpenGL timer queries) and the time from reading the input to the GPU finishing that frame (a stand-in
// for input-to-screen latency). tools/bench/frames.py turns the file into percentiles.
#pragma once
#include <string>

namespace dq::bench {
enum Phase { Wait, Input, Update, Render, Ui, Swap, Sync, Sleep, PhaseCount };
bool on();
void frameBegin();            // the top of the loop
void mark(Phase p);           // the time since the last mark goes to p
void inputSampled();          // the moment the input for this frame was read (after the event poll)
void scope(const std::string& name, double ms);   // a named piece (session.cpp's profile scopes), for spike blame
void frameEnd(bool gl);       // after the limiter: writes the line (gl: GPU timestamps on this frame)
}
