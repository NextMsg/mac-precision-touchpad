// PTP_CONTACT/PTP_REPORT must be defined before including this header.
#pragma once
typedef struct _AMT_CONTACT_LIFECYCLE {
    PTP_REPORT LastReport;
    unsigned int PendingUp;
    unsigned int Suppressed;
} AMT_CONTACT_LIFECYCLE;

static inline void AmtContactObserve(AMT_CONTACT_LIFECYCLE* state, unsigned int down)
{
    // Remember lifts even when Windows has no read pending, including ID reuse.
    for (unsigned int i = 0; i < state->LastReport.ContactCount; ++i) {
        const PTP_CONTACT* old = &state->LastReport.Contacts[i];
        if (old->TipSwitch && !(down & (1u << old->ContactID))) {
            state->PendingUp |= 1u << old->ContactID;
        }
    }
    state->Suppressed &= down;
}

static inline void AmtContactBuild(
    AMT_CONTACT_LIFECYCLE* state, const PTP_CONTACT* byId,
    const unsigned int* order, unsigned int orderCount, unsigned int down,
    int surfaceEnabled, PTP_REPORT* report)
{
    unsigned int seen = 0, ongoing = 0;
    if (!surfaceEnabled) return;
    // Existing contacts retain priority and get exactly one UP at their last
    // delivered position. Hover coordinates never become cursor motion.
    for (unsigned int i = 0; i < state->LastReport.ContactCount; ++i) {
        const PTP_CONTACT* old = &state->LastReport.Contacts[i];
        if (!old->TipSwitch) continue;
        unsigned int bit = 1u << old->ContactID;
        PTP_CONTACT* dst = &report->Contacts[report->ContactCount++];
        seen |= bit;
        if ((down & bit) && !(state->PendingUp & bit)) {
            *dst = byId[old->ContactID];
            ++ongoing;
        } else {
            *dst = *old;
            dst->TipSwitch = 0;
        }
    }
    for (unsigned int i = 0; i < orderCount; ++i) {
        unsigned int id = order[i], bit = 1u << id;
        if (!(down & bit) || (seen & bit) || (state->Suppressed & bit)) continue;
        seen |= bit;
        if (ongoing >= 5) {
            // An excess contact stays suppressed until it physically lifts.
            state->Suppressed |= bit;
        } else if (report->ContactCount < 5) {
            report->Contacts[report->ContactCount++] = byId[id];
            ++ongoing;
        }
        // UP records can occupy this frame's slots. Defer a new DOWN to the
        // next frame instead of losing the UP or merging two contact lifetimes.
    }
}

static inline void AmtContactCommit(AMT_CONTACT_LIFECYCLE* state, const PTP_REPORT* report)
{
    unsigned int down = 0;
    for (unsigned int i = 0; i < report->ContactCount; ++i) {
        if (report->Contacts[i].TipSwitch) down |= 1u << report->Contacts[i].ContactID;
    }
    state->PendingUp &= down;
    state->LastReport = *report;
}
