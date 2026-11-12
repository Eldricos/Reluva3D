# LEGO Alpha Team: speed, as it went

- Speed, as it went (the latest at the end of this list), measured
  headless (frames given to the engine, a click past the demo). At first
  the menu at headquarters ran at about 17 frames a second on 4
  cores, after about 20 seconds of loading (scaled from 14 and 22 by the
  latest measurements below).
  - Inlining instruction execution (memory access, fetching, operands)
    and the rasterizer's per-draw set-up got it there from about half
    that.
  - The decoded-instruction cache (x86.md) cut the time to reach the menu
    from about 41 seconds to 28.
  - The rasterizer then took a third or so of the time at the menu. The
    rasterizer's notes ([directx.md](../../../../../common/x86/directx.md)) have what was done; drawing 120 frames went
    from 7.4 seconds to 2.0. The interpreter is now most of the time.
  - Checking the speed-ups: with the clock driven by the instruction count
    and the click at a set frame, runs repeat exactly. Frames saved by the
    game's own frame number came out byte-identical before and after each
    change. The engine's frame count doesn't do for this: which game frame
    it shows depends on timing.
  - Then, measured in the training mission (600 frames from the start, a
    run that's two thirds loading and the menus): 42.0 seconds.
    - Blocks in the instruction cache (x86.md): 29.6 seconds.
    - Drawing on threads of their own, in order, while the program goes on
      (the rasterizer's notes, [directx.md](../../../../../common/x86/directx.md)): 25.5 seconds.
  - Then, where the interpreter's time went while playing, sampled by
    the program's own addresses: over a quarter of it was in
    `DDCycleCounter::Start` and `Stop` (DDPackedDirFileLib). They time the
    program's work with RDTSC around a great many of its functions, for a
    profile only the interface behaviour reads (`Get`, `Zero`; PE0300
    `0x10065940`, which shows it only when a flag at `+0x31` is set).
    `GameProcess` makes them return at once (both cdecl, void); the profile
    then reads zero. Mission frames went from 32 a second to 42 (780
    frames, 24.3 seconds to 18.5).
    - With fewer instructions to a frame, a fixed clock moves less in a
      frame: the game's timed waits (the loading screen before
      headquarters) take more frames, so the routes' frame numbers changed.
      The fixed clock now also counts the main thread's `Sleep`s, as the
      time that would have passed.
  - Then loading. Over a third of what loading ran was in our MSVCP60's
    `istream::read`, by our own DLL's link map. The archives aren't read a
    byte at a time, though: `DDMappedFileBuf` (DDPackedDirFileLib) maps
    the whole file and makes it the stream's get area, leaving `xsgetn` to
    `streambuf`'s. The cost was in the number of reads: NetImmerse reads
    its files a field at a time, about 2.5 million `read`s of a few bytes
    before the first frame. Each went through the sentry, the virtual
    `xsgetn` and its loop.
    - `read` now copies straight from the get area when the stream is good,
      not tied, the get area holds all `n` bytes, and the buffer keeps our
      `xsgetn`. The archive DLL's vftable doesn't hold our function's
      address but its own `jmp [import]` stub for it, so the check follows
      that.
    - Start-up to the first frames went from 377 to 278 million
      instructions and from 1.80 to 1.53 seconds. Loading the training
      mission went from 252 to 175 million instructions.
    - Copying 4-byte reads in our own code instead of calling MSVCRT's
      `memcpy` was no faster: a call to a stand-in is one instruction to the
      interpreter and runs natively.
  - Then, measured while Dash runs in Mission One (frames 3421 to 3600,
    `at-run -profile`, and the drawing threads sampled with gdb): the
    program's thread spent over a third of its time in `Flip`, waiting for
    the frame's drawing to finish, about 12 ms a frame. The interpreter
    wasn't the limit; the drawing was. (Drawing small draws as one batch
    on every row thread changed nothing: the wait was for the frame's
    drawing as a whole.)
    - `Clear` is now queued with the draws, filled by rows as they are,
      instead of waiting for them: the same pixels, and no wait at each
      frame's start.
    - `DirectX::presentLate`: a flip shows the frame before (waiting only
      for the draws queued before its flip) and lets this frame's drawing
      go on while the program works out the next. The player shows frames
      this way; frames come a flip later, about 30 ms. Those 180 frames
      went from 5.1 s to 3.9 s, Mission One's whole route from 2 min 3 s
      to 1 min 28 s, with the same instructions run (frame numbers count
      a flip later, so a click given by frame number comes a flip later
      too). `at-run -present-late` and `at-sweep` run that way; `at-run`
      without it, and the tests, show each frame at its own flip.
  - Then, profiled again: callgrind's instruction counts, one run
    counting only the drawing threads (`--toggle-collect` on
    `DrawQueue::draw`) and one only the interpreter (`Cpu::runCached`),
    and each thread's CPU time from `/proc`. Up to the headquarters menu
    the drawing threads used twice the CPU time of the program's thread,
    which waited for them about a sixth of the time at each flip. The
    rasterizer drew about 940 triangles a frame but tested about 916,000
    pixels (three times the screen), nearly all bilinear and on the
    simple texture-stage path.
    - The pixel's steps (depth test, texture stages, sampling, the rest)
      are inlined into the triangle's loop; their calls had cost more
      than much of their work.
    - Bilinear samples that cross a wrapping texture's edge took the
      general path, texel by texel; they're now addressed by masks.
    - 16-bit frames and textures convert to 32-bit colour by one table
      per format; clears store whole pixels; clipping a triangle no
      longer allocates.
    - The interpreter's loop spent about 12 host instructions of each
      guest instruction on bookkeeping. It now counts a block's
      instructions at once, and takes the parity flag from a table
      (x86.md).
    - Measured on the M1010 solved route (`at-run ... -solved M1010`):
      frames 600 to 1200, at the headquarters menu, went from 13.1 s to
      10.6 s; the route to frame 3300 from 76.7 s to 68.0 s, with about
      15% less CPU time over all threads. Loading (to frame 600) went from
      20.8 s to 19.3 s. Each the mean of two runs; the machine varies by
      up to 10% run to run. The same instructions ran (8,763,980,719 to
      frame 3300) and the frames saved every 300 came out byte-identical.
  - Then, from playing it in a Release build on Windows: over 1 GB of
    memory and half the processor.
    - The program's 1 GB of memory was a zero-filled `std::vector`,
      written through at start-up. It now comes zeroed from the system
      (`mmap`, `VirtualAlloc`) through `PageAllocator` (x86.md), so only
      the pages the program uses are there: the M1010 route's peak went
      from 1,106 MB to 130 MB, with the same frames.
    - The game draws as fast as it can (its main thread asked to sleep
      500 ms in 1,300 frames, `at-run` now says), counting on `Flip` to
      wait for the display's vertical blank. Ours didn't, so a fast
      machine drew frames nobody saw. The player now shows at most one a
      60th of a second, as a 60 Hz display would; `at-run` still runs
      flat out.
  - Then at 1024x768x32, the resolution most players choose: in
    Mission One (`Width  1024`, `Height  768`, `BPP  32` in `Game.txt`)
    the program's thread waited at a third of its flips for the frame
    before to be drawn, while the drawing threads were idle a third of
    their time: they drew in batches, each waiting at the end of each
    for the slowest. They now go through the draws each at its own pace
    (each still draws only its own rows, every draw in turn): the
    route from 106 s to 95 s on 4 cores, with the same frames.
  - A profile at 1024x768x32 (28 frames a second) still had the
    program's thread busy four fifths of the time and the drawing
    threads half. One block run in four checked its bytes again, because
    operator new, free and Direct3D's setters and draws, called millions
    of times on the route, counted as natives that might have written
    code; and every load and store asked whether its address was in
    memory with three compares. Those natives are now known to write
    only what's noted, and the question is one compare: the program's
    thread through the route from 54.8 s to 46.3 s, with the same
    instructions and frames.
  - With that, the drawing was the limit: on 4 cores the program's
    thread spent 30% of its time in `Flip` waiting for the frame before
    to be drawn (only 1.2 s of the route in waits mid-frame), and the
    profile at 1024x768 had the drawing threads using about four cores.
    Of the pixels drawn, 47% take three bilinear texture stages and 37%
    one stage blended. Drawing four pixels at a time (`Draw::span4`)
    took the drawing threads' time on the route from 276 s to 184 s and
    the route's wall time from 98 s to 75 s, with the same frames at
    both resolutions.
  - The blocks as native code (`common/x86/jit.cpp`, docs/common/x86.md):
    moves, LEA, PUSH and POP made host instructions, the rest called
    directly; then the ALU and jumps made too, with flags only where
    read, and blocks going on to the next in the native code. The
    program's thread on the route 26% quicker than with the
    interpreter's loop (40 s from 54 s), the same instructions and
    frames. A profile at 1024x768 then showed 35 to 40 frames a second,
    and 45 in the game (35 to 50) with the dispatch in native code.
    Then blocks kept aside rather than made again, and the x87 unit
    made: 35 s on the route, from 54 s with the interpreter.
  - The attract sequence before the menu (example levels played with
    scripted cameras, loading each) ran at 30 frames a second, the
    program's thread busy four fifths of the time: four times the
    instructions of a mission's frame. Timing every native there found
    a quarter of the thread in them: operator new and free (`Arena`'s
    first fit walked every free block below the one it took; now
    blocks are also kept by size, finding the same block), creating and
    releasing vertex buffers (COM's pointers were searched one by one
    for an object's; now by object, freed in the same order), and 33
    million memcpys of a few bytes, the streams' reads, each going back
    out of the native code (now called from its dispatch). The heap
    hands out the same addresses, so the instructions and frames are
    the same. That gave 35 frames a second there (25 to 42); the next
    profile had finding each next block a seventh of the thread, now
    mostly gone (blocks linked to the next, docs/common/x86.md). Then
    38 (25 to 50): the slowest stretches, two or three times the
    others' time, are each cut to the next example level, as it loads.
    Then 42 (27 to 55), with shifts and the x87 compares made; the
    blocks' table made two-way after that. `RELUVA_NO_JIT=1` in the environment runs
    without it, to compare.
- Not there yet (speed, parked for now):
  - **Wide camera shots with much on screen** are the slowest: the
    program's own work (NetImmerse's scene graph, culling and picking)
    and the drawing are both at their busiest there.
  - Ways on, each measured against the frames staying the same: in the
    JIT, keeping the emulated registers in the host's within a block, and
    predicting where a RET goes (finding the next block is still about a
    tenth of the program's thread, mostly after returns); for the
    drawing, the graphics card instead of the processor, which would end
    the frames being checked byte for byte.
  - What differs from the original because of how it runs is in
    [../KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md).
