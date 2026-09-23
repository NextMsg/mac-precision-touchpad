// Magic Trackpad 2 contact-lifetime rejection. No WDF dependencies.
#pragma once

// Percentage of each axis reserved at BOTH ends. Zero disables edge rejection.
#ifndef AMT_PTP_EDGE_PERCENT
#define AMT_PTP_EDGE_PERCENT 5
#endif
#if AMT_PTP_EDGE_PERCENT < 0 || AMT_PTP_EDGE_PERCENT >= 50
#error AMT_PTP_EDGE_PERCENT must be between 0 and 49
#endif

// A single area spike (especially around lift) must not cancel a scroll.
// Three consecutive confirmed-touch frames retain rejection for a sustained palm.
#define AMT_PTP_SIZE_CONFIRM_FRAMES 3
typedef struct _AMT_EDGE_REJECTION_STATE {
    unsigned int Active;
    unsigned int Rejected;
    unsigned int FrameActive;
    unsigned char LargeFrames[16];
} AMT_EDGE_REJECTION_STATE;

static inline void AmtEdgeReset(AMT_EDGE_REJECTION_STATE* state)
{
    state->Active = state->Rejected = state->FrameActive = 0;
    for (unsigned int i = 0; i < 16; ++i) state->LargeFrames[i] = 0;
}

static inline void AmtEdgeBeginFrame(AMT_EDGE_REJECTION_STATE* state)
{
    state->FrameActive = 0;
}

static inline int AmtEdgeConfidence(
    AMT_EDGE_REJECTION_STATE* state, unsigned int id,
    int x, int y, int width, int height, int touching, int sizeConfidence)
{
    unsigned int bit;
    int edge = 0;

    // The Type 5 protocol has a four-bit contact ID.
    if (id >= 16) return 0;
    bit = 1u << id;
#if AMT_PTP_EDGE_PERCENT > 0
    if (width > 0 && height > 0) {
        int marginX = width * AMT_PTP_EDGE_PERCENT / 100;
        int marginY = height * AMT_PTP_EDGE_PERCENT / 100;
        edge = x <= marginX || x >= width - marginX ||
               y <= marginY || y >= height - marginY;
    }
#else
    (void)x;
    (void)y;
    (void)width;
    (void)height;
#endif

    if (touching) {
        state->FrameActive |= bit;
        if (sizeConfidence) state->LargeFrames[id] = 0;
        else if (state->LargeFrames[id] < AMT_PTP_SIZE_CONFIRM_FRAMES) {
            ++state->LargeFrames[id];
        }
        if (state->LargeFrames[id] >= AMT_PTP_SIZE_CONFIRM_FRAMES ||
            (!(state->Active & bit) && edge)) {
            state->Rejected |= bit;
        }
    }

    // Preserve rejection on the UP report too. Never resurrect a palm as it moves.
    return !(state->Rejected & bit);
}

static inline void AmtEdgeEndFrame(AMT_EDGE_REJECTION_STATE* state)
{
    state->Active = state->FrameActive;
    state->Rejected &= state->Active;
    for (unsigned int i = 0; i < 16; ++i) {
        if (!(state->Active & (1u << i))) state->LargeFrames[i] = 0;
    }
}
