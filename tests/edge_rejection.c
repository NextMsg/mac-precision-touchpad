// Host-side behavioral tests. WDF is mocked; the Type 5 handler is extracted
// verbatim from InputInterrupt.c by run_edge_rejection.py.
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "EdgeRejection.h"

#ifndef _In_
#define _In_
#endif
#define TRUE 1
#define NT_SUCCESS(s) ((s) >= 0)
#define STATUS_DEVICE_DATA_ERROR (-1)
#define FSIZE_TYPE5 9
#define MAX_FINGERS 16
#define PTP_MAX_CONTACT_POINTS 5
#define REPORTID_MULTITOUCH 1
#define TraceEvents(...) ((void)0)
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef int16_t SHORT;
typedef uint32_t UINT;
typedef uint32_t ULONG;
typedef int32_t INT;
typedef int NTSTATUS;
typedef long long LONGLONG;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef int WDFREQUEST;
typedef int WDFMEMORY;
#include "ptp_report.inc"
#include "ContactLifecycle.h"
_Static_assert(sizeof(PTP_CONTACT) == 9, "PTP contact wire size");
_Static_assert(sizeof(PTP_REPORT) == 50, "PTP report wire size");
_Static_assert(offsetof(PTP_REPORT, ScanTime) == 46, "PTP ScanTime wire offset");
typedef struct { int min, max; } AXIS;
typedef struct { int tp_header, tp_fsize, tp_delta, tp_button; AXIS x, y; } CONFIG;
typedef struct {
    const CONFIG* DeviceInfo;
    int IsSurfaceReportOn, IsButtonReportOn, InputQueue;
    LARGE_INTEGER PerfCounter;
    AMT_EDGE_REJECTION_STATE EdgeRejection;
    AMT_CONTACT_LIFECYCLE ContactLifecycle;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;
static int pending, memoryFailure, copyFailure, completed;
static NTSTATUS completionStatus;
static size_t information;
static PTP_REPORT output;
static LONGLONG counterTicks = 100;
static LONGLONG counterFrequency = 10000000;
static NTSTATUS WdfIoQueueRetrieveNextRequest(int queue, WDFREQUEST* request)
{ (void)queue; *request = 1; return pending ? 0 : -2; }
static NTSTATUS WdfRequestRetrieveOutputMemory(WDFREQUEST request, WDFMEMORY* memory)
{ (void)request; *memory = 1; return memoryFailure ? -3 : 0; }
static void QueryPerformanceCounter(LARGE_INTEGER* value) { value->QuadPart = counterTicks; }
static void QueryPerformanceFrequency(LARGE_INTEGER* value) { value->QuadPart = counterFrequency; }
static NTSTATUS WdfMemoryCopyFromBuffer(WDFMEMORY memory, size_t offset, const void* data, size_t size)
{ (void)memory; (void)offset; if (copyFailure) return -4; memcpy(&output, data, size); return 0; }
static void WdfRequestSetInformation(WDFREQUEST request, size_t size)
{ (void)request; information = size; }
static void WdfRequestComplete(WDFREQUEST request, NTSTATUS status)
{ (void)request; completed++; completionStatus = status; }
#include "type5_handler.inc"

static const CONFIG config = {12, 9, 0, 1, {-3678, 3934}, {-2479, 2586}};
static DEVICE_CONTEXT device;
static UCHAR packet[12 + 9 * 17];
static void reset(void)
{
    memset(&device, 0, sizeof(device));
    device.DeviceInfo = &config;
    device.IsSurfaceReportOn = device.IsButtonReportOn = 1;
    AmtEdgeReset(&device.EdgeRejection);
    memset(packet, 0, sizeof(packet));
    pending = 1; memoryFailure = copyFailure = 0;
}
static void finger(int slot, unsigned int id, int x, int y, int down, int minor)
{
    UCHAR* p = packet + 12 + 9 * slot;
    int rx = x + config.x.min;
    // Inverse of the existing Type 5 packed Y conversion.
    int ry = -(y + config.y.min) - ((rx & 0x1fff) != 0);
    uint32_t packed = ((uint32_t)rx & 0x1fff) | (((uint32_t)ry & 0x1fff) << 13);
    p[0] = (UCHAR)packed; p[1] = (UCHAR)(packed >> 8);
    p[2] = (UCHAR)(packed >> 16); p[3] = (UCHAR)(packed >> 24);
    p[3] |= down ? 0x80 : 0xc0;
    p[4] = down ? 40 : 0; p[5] = (UCHAR)minor;
    p[8] = (UCHAR)id;
}
static NTSTATUS frame(int contacts)
{
    completed = 0; information = 0;
    memset(&output, 0xCC, sizeof(output));
    return AmtPtpServiceTouchInputInterruptType5(&device, packet, 12 + 9 * contacts);
}
static void verify(void)
{
    assert(completed == 1 && completionStatus == 0 && information == sizeof(output));
}

#include "replay_trace.inc"

int main(void)
{
    const int edgeConfidence = AMT_PTP_EDGE_PERCENT == 0;
    // A central finger remains valid while an edge wrist arrives and drifts inward.
    reset(); finger(0, 3, 3000, 2000, 1, 30); assert(frame(1) == 0); verify();
    assert(output.Contacts[0].X == 3000 && output.Contacts[0].Y == 2000);
    finger(1, 9, 20, 2000, 1, 30); assert(frame(2) == 0);
    assert(output.ContactCount == 2 && output.Contacts[0].Confidence == 1);
    assert(output.Contacts[1].Confidence == edgeConfidence && output.Contacts[1].TipSwitch == 1);
    // Reordered slots must not swap the rejection state.
    finger(0, 9, 2000, 2000, 1, 30); finger(1, 3, 20, 2000, 1, 30); frame(2);
    assert(output.Contacts[1].Confidence == edgeConfidence && output.Contacts[0].Confidence == 1);
    // Keep confidence low on UP; permit a new central contact with the same ID.
    finger(0, 9, 2000, 2000, 0, 0); frame(2);
    assert(output.Contacts[1].Confidence == edgeConfidence && output.Contacts[1].TipSwitch == 0);
    finger(0, 9, 2000, 2000, 1, 30); frame(2); assert(output.Contacts[1].Confidence == 1);

    // Each of four boundaries is inclusive; just inside it is usable.
    { const int positions[][2] = {{380,2500},{7232,2500},{3800,253},{3800,4812},
                                 {381,2500},{7231,2500},{3800,254},{3800,4811}};
      for (int i = 0; i < 8; ++i) {
          reset(); finger(0, 15, positions[i][0], positions[i][1], 1, 30); frame(1);
          assert(output.Contacts[0].Confidence == (i < 4 ? edgeConfidence : 1));
      }
    }
    // Multiple intentional fingers in the center still work.
    reset();
    for (int i = 0; i < 5; ++i) finger(i, (unsigned int)i, 2000 + i * 400, 2000, 1, 30);
    frame(5); for (int i = 0; i < 5; ++i) assert(output.Contacts[i].Confidence == 1);

    // Size-based palms remain rejected for their lifetime even after shrinking.
    reset(); finger(0, 2, 3000, 2000, 1, 173);
    frame(1); assert(output.Contacts[0].Confidence);
    frame(1); assert(output.Contacts[0].Confidence);
    frame(1); assert(!output.Contacts[0].Confidence);
    finger(0, 2, 3000, 2000, 1, 20); frame(1); assert(!output.Contacts[0].Confidence);
    frame(0); frame(1); assert(output.Contacts[0].Confidence);
    // Recorded scrolling failure: one transient area spike used to poison
    // the contact until lift, even after normal area returned.
    reset(); finger(0, 7, 3674, 3745, 1, 122);
    finger(1, 11, 3654, 3651, 1, 174); frame(2);
    assert(output.Contacts[0].Confidence && output.Contacts[1].Confidence);
    finger(1, 11, 3680, 3600, 1, 120); frame(2);
    assert(output.Contacts[1].Confidence);
    // Two noisy scans do not accumulate across a normal frame.
    finger(1, 11, 3700, 3500, 1, 174); frame(2); frame(2);
    finger(1, 11, 3700, 3500, 1, 120); frame(2);
    finger(1, 11, 3700, 3500, 1, 174); frame(2);
    assert(output.Contacts[1].Confidence);
    // Hover/start/lift states with nonzero area must not trigger size rejection.
    for (int state = 0; state <= 0xc0; state += 0x40) {
        if (state == 0x80) continue;
        reset(); finger(0, 7, 3594, 3889, 1, 175);
        packet[15] = (UCHAR)((packet[15] & 0x3f) | state);
        for (int i = 0; i < 8; ++i) frame(1);
        assert(output.ContactCount == 0);
    }
    // A noisy UP must not turn a valid gesture into an accidental contact.
    reset(); finger(0, 1, 3000, 2000, 1, 30); frame(1);
    finger(0, 1, 3000, 2000, 0, 200); frame(1);
    assert(output.Contacts[0].Confidence && !output.Contacts[0].TipSwitch);
    // Disappearance clears a partially accumulated size decision on ID reuse.
    reset(); finger(0, 1, 3000, 2000, 1, 200); frame(1); frame(1); frame(0);
    frame(1); assert(output.Contacts[0].Confidence);
    // No host read: still classify starts and observe disappearance/ID reuse.
    reset(); pending = 0; finger(0, 6, 20, 2000, 1, 30);
    assert(frame(1) == -2 && completed == 0);
    finger(0, 6, 3000, 2000, 1, 30); pending = 1; frame(1);
    assert(output.Contacts[0].Confidence == edgeConfidence);
    pending = 0; frame(0); pending = 1; frame(1);
    assert(output.ContactCount == 1 && !output.Contacts[0].TipSwitch);
    frame(1); assert(output.Contacts[0].Confidence && output.Contacts[0].TipSwitch);
    // Track contacts outside the five output slots too.
    reset(); for (int i = 0; i < 6; ++i) finger(i, (unsigned int)i, i == 5 ? 0 : 2000, 2000, 1, 30);
    frame(6); assert(output.ContactCount == 5);
    finger(0, 5, 3000, 2000, 1, 30); frame(1);
    assert(output.ContactCount == 5);
    for (int i = 0; i < 5; ++i) assert(!output.Contacts[i].TipSwitch);
    frame(1); assert(output.ContactCount == 0); // sixth contact remains suppressed
    frame(0); frame(1); assert(output.ContactCount == 1 && output.Contacts[0].Confidence);
    // Disabled surface reports contain no stale contacts, but retain lifetime state.
    reset(); device.IsSurfaceReportOn = 0; packet[1] = 1;
    finger(0, 6, 0, 2000, 1, 30); frame(1);
    assert(output.ContactCount == 0 && output.IsButtonClicked == 1);
    for (int i = 0; i < 5; ++i) assert(output.Contacts[i].TipSwitch == 0);
    device.IsSurfaceReportOn = 1; finger(0, 6, 3000, 2000, 1, 30); frame(1);
    assert(output.Contacts[0].Confidence == edgeConfidence);
    AmtEdgeReset(&device.EdgeRejection); frame(1); assert(output.Contacts[0].Confidence);
    // Invalid reports do not consume requests; output failures complete once.
    reset(); completed = 0;
    assert(AmtPtpServiceTouchInputInterruptType5(&device, packet, 11) == STATUS_DEVICE_DATA_ERROR);
    assert(AmtPtpServiceTouchInputInterruptType5(&device, packet, 13) == STATUS_DEVICE_DATA_ERROR);
    assert(frame(17) == STATUS_DEVICE_DATA_ERROR && completed == 0);
    memoryFailure = 1; assert(frame(0) == -3 && completed == 1 && completionStatus == -3);
    memoryFailure = 0; copyFailure = 1;
    assert(frame(0) == -4 && completed == 1 && completionStatus == -4 && information == 0);
    // Real counter frequencies, time beyond the old 255 clamp, and 16-bit wrap.
    reset(); counterFrequency = 10000000; counterTicks = 30000000; frame(0);
    assert(output.ScanTime == 30000);
    counterTicks = 70000000; frame(0); assert(output.ScanTime == 4464);
    counterFrequency = 3579545; counterTicks = counterFrequency + counterFrequency / 2;
    frame(0); assert(output.ScanTime == 14999);
    // Extreme packed Y previously negated INT_MIN. Both axes remain in the HID range.
    reset(); packet[15] = 0x82; frame(1);
    assert(output.Contacts[0].Y == 5065);
    for (int coordinate = 0; coordinate < 8192; ++coordinate) {
        uint32_t packed = (uint32_t)coordinate | ((uint32_t)coordinate << 13);
        packet[12] = (UCHAR)packed; packet[13] = (UCHAR)(packed >> 8);
        packet[14] = (UCHAR)(packed >> 16); packet[15] = (UCHAR)((packed >> 24) | 0x80);
        frame(1);
        assert(output.Contacts[0].X <= 7612 && output.Contacts[0].Y <= 5065);
    }
    // Exact user reproduction: the first finger is stationary; a second finger
    // approaches without touching. Nonzero hover area must not add a contact.
    reset(); finger(0, 3, 3000, 2000, 1, 40); frame(1);
    for (int state = 0; state <= 0xc0; state += 0x40) {
        if (state == 0x80) continue;
        for (int step = 0; step < 20; ++step) {
            finger(1, 9, 4000 + step * 20, 2500 + step * 10, 1, 80);
            packet[24] = (UCHAR)((packet[24] & 0x3f) | state);
            frame(2);
            assert(output.ContactCount == 1 && output.Contacts[0].ContactID == 3);
            assert(output.Contacts[0].X == 3000 && output.Contacts[0].Y == 2000);
        }
    }
    // Confirmed touch with zero area is still a valid DOWN; state is authoritative.
    finger(1, 9, 4000, 2500, 1, 80); packet[25] = 0; frame(2);
    assert(output.ContactCount == 2 && output.Contacts[1].TipSwitch);
    // Transition to hover emits exactly one UP at the last delivered XY.
    finger(1, 9, 4300, 2700, 1, 80); packet[24] &= 0x3f; frame(2);
    assert(output.ContactCount == 2 && !output.Contacts[1].TipSwitch);
    assert(output.Contacts[1].X == 4000 && output.Contacts[1].Y == 2500);
    frame(2); assert(output.ContactCount == 1);
    // Hovering at an edge must not poison a subsequent central physical DOWN.
    reset(); finger(0, 6, 0, 2000, 1, 40); packet[15] &= 0x3f; frame(1);
    assert(output.ContactCount == 0);
    finger(0, 6, 3000, 2000, 1, 40); frame(1); assert(output.Contacts[0].Confidence);
    // Failed output must not advance host tracking or lose a necessary UP.
    finger(0, 6, 3100, 2100, 1, 40); copyFailure = 1; frame(1); copyFailure = 0;
    finger(0, 6, 3200, 2200, 0, 40); frame(1);
    assert(!output.Contacts[0].TipSwitch && output.Contacts[0].X == 3000 && output.Contacts[0].Y == 2000);
    frame(1); assert(output.ContactCount == 0);
    // Disappearance without an explicit raw UP still releases the last contact.
    reset(); finger(0, 1, 3000, 2000, 1, 40); frame(1); frame(0);
    assert(output.ContactCount == 1 && !output.Contacts[0].TipSwitch);
    frame(0); assert(output.ContactCount == 0);
    replay_trace();
    printf("PASS: Type 5 contact scenarios (edge percent = %d)\n", AMT_PTP_EDGE_PERCENT);
    return 0;
}
