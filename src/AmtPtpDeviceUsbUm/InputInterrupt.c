// InputInterrupt.c: Handles device input event

#include <driver.h>
#include "InputInterrupt.tmh"

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpConfigContReaderForInterruptEndPoint(
	_In_ PDEVICE_CONTEXT DeviceContext
)
{

	WDF_USB_CONTINUOUS_READER_CONFIG contReaderConfig;
	NTSTATUS status;
	size_t transferLength = 0;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	switch (DeviceContext->DeviceInfo->tp_type)
	{
		case TYPE1:
			transferLength = HEADER_TYPE1 + FSIZE_TYPE1 * MAX_FINGERS;
			break;
		case TYPE2:
			transferLength = HEADER_TYPE2 + FSIZE_TYPE2 * MAX_FINGERS;
			break;
		case TYPE3:
			transferLength = HEADER_TYPE3 + FSIZE_TYPE3 * MAX_FINGERS;
			break;
		case TYPE4:
			transferLength = HEADER_TYPE4 + FSIZE_TYPE4 * MAX_FINGERS;
			break;
		case TYPE5:
			transferLength = HEADER_TYPE5 + FSIZE_TYPE5 * MAX_FINGERS;
			break;
	}

	if (transferLength <= 0) {
		status = STATUS_UNKNOWN_REVISION;
		return status;
	}

	WDF_USB_CONTINUOUS_READER_CONFIG_INIT(
		&contReaderConfig,
		AmtPtpEvtUsbInterruptPipeReadComplete,
		DeviceContext,		// Context
		transferLength		// Calculate transferred length by device information
	);

	contReaderConfig.EvtUsbTargetPipeReadersFailed = AmtPtpEvtUsbInterruptReadersFailed;
	if (DeviceContext->DeviceInfo->tp_type == TYPE5) {
		// Contact lifetimes require ordered, non-overlapping frame callbacks.
		contReaderConfig.NumPendingReads = 1;
	}

	// Remember to turn it on in D0 entry
	status = WdfUsbTargetPipeConfigContinuousReader(
		DeviceContext->InterruptPipe,
		&contReaderConfig
	);

	if (!NT_SUCCESS(status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! AmtPtpConfigContReaderForInterruptEndPoint failed with Status code %!STATUS!",
			status
		);
		return status;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);

	return STATUS_SUCCESS;

}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
AmtPtpEvtUsbInterruptPipeReadComplete(
	_In_ WDFUSBPIPE  Pipe,
	_In_ WDFMEMORY   Buffer,
	_In_ size_t      NumBytesTransferred,
	_In_ WDFCONTEXT  Context
)
{
	UNREFERENCED_PARAMETER(Pipe);

	WDFDEVICE       device;
	PDEVICE_CONTEXT pDeviceContext = Context;
	UCHAR*			pBuffer = NULL;
	NTSTATUS        status;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	device = WdfObjectContextGetObject(pDeviceContext);
	size_t headerSize = (unsigned int) pDeviceContext->DeviceInfo->tp_header;
	size_t fingerprintSize = (unsigned int) pDeviceContext->DeviceInfo->tp_fsize;

	if (NumBytesTransferred < headerSize || (NumBytesTransferred - headerSize) % fingerprintSize != 0) {

		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! Malformed input received. Length = %llu. Attempt to reset device.",
			NumBytesTransferred
		);

		status = AmtPtpEmergResetDevice(pDeviceContext);
		if (!NT_SUCCESS(status)) {

			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_DRIVER,
				"%!FUNC! AmtPtpEmergResetDevice failed with %!STATUS!",
				status
			);

		}

		return;
	}

	if (!pDeviceContext->IsWellspringModeOn) {

		TraceEvents(
			TRACE_LEVEL_WARNING,
			TRACE_DRIVER,
			"%!FUNC! Routine is called without enabling Wellspring mode"
		);

		return;
	}

	// Dispatch USB Interrupt routine by device family
	switch (pDeviceContext->DeviceInfo->tp_type) {
		case TYPE1:
		{
			TraceEvents(
				TRACE_LEVEL_WARNING,
				TRACE_DRIVER,
				"%!FUNC! Mode not yet supported"
			);
			break;
		}
		// Universal routine handler
		case TYPE2:
		case TYPE3:
		case TYPE4:
		{
			pBuffer = WdfMemoryGetBuffer(
				Buffer,
				NULL
			);

			status = AmtPtpServiceTouchInputInterrupt(
				pDeviceContext,
				pBuffer,
				NumBytesTransferred
			);

			if (!NT_SUCCESS(status)) {
				TraceEvents(
					TRACE_LEVEL_WARNING,
					TRACE_DRIVER,
					"%!FUNC! AmtPtpServiceTouchInputInterrupt failed with %!STATUS!",
					status
				);
			}
			break;
		}
		// Magic Trackpad 2
		case TYPE5:
		{
			pBuffer = WdfMemoryGetBuffer(
				Buffer,
				NULL
			);
			status = AmtPtpServiceTouchInputInterruptType5(
				pDeviceContext,
				pBuffer,
				NumBytesTransferred
			);

			if (!NT_SUCCESS(status)) {
				TraceEvents(
					TRACE_LEVEL_WARNING,
					TRACE_DRIVER,
					"%!FUNC! AmtPtpServiceTouchInputInterrupt5 failed with %!STATUS!",
					status
				);
			}
			break;
		}
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);

}

_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN
AmtPtpEvtUsbInterruptReadersFailed(
	_In_ WDFUSBPIPE Pipe,
	_In_ NTSTATUS Status,
	_In_ USBD_STATUS UsbdStatus
)
{
	UNREFERENCED_PARAMETER(Pipe);
	UNREFERENCED_PARAMETER(UsbdStatus);
	UNREFERENCED_PARAMETER(Status);

	return TRUE;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpServiceTouchInputInterrupt(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ UCHAR* Buffer,
	_In_ size_t NumBytesTransferred
)
{
	NTSTATUS Status;
	WDFREQUEST Request;
	WDFMEMORY  RequestMemory;
	PTP_REPORT PtpReport;
	LARGE_INTEGER CurrentPerfCounter;
	LONGLONG PerfCounterDelta;

	const struct TRACKPAD_FINGER *f;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	size_t raw_n, i = 0;
	size_t headerSize = (unsigned int) DeviceContext->DeviceInfo->tp_header;
	size_t fingerprintSize = (unsigned int) DeviceContext->DeviceInfo->tp_fsize;
	USHORT x = 0, y = 0;

	Status = STATUS_SUCCESS;
	PtpReport.ReportID = REPORTID_MULTITOUCH;
	PtpReport.IsButtonClicked = 0;

	// Retrieve next PTP touchpad request.
	Status = WdfIoQueueRetrieveNextRequest(
		DeviceContext->InputQueue,
		&Request
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! No pending PTP request. Interrupt disposed"
		);
		goto exit;
	}

	QueryPerformanceCounter(
		&CurrentPerfCounter
	);

	// Scan time is in 100us
	PerfCounterDelta = (CurrentPerfCounter.QuadPart - DeviceContext->PerfCounter.QuadPart) / 100;
	// Only two bytes allocated
	if (PerfCounterDelta > 0xFF)
	{
		PerfCounterDelta = 0xFF;
	}

	PtpReport.ScanTime = (USHORT) PerfCounterDelta;

	// Allocate output memory.
	Status = WdfRequestRetrieveOutputMemory(
		Request,
		&RequestMemory
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! WdfRequestRetrieveOutputMemory failed with %!STATUS!",
			Status
		);
		goto exit;
	}

	// Type 2 touchpad surface report
	if (DeviceContext->IsSurfaceReportOn) {
		// Handles trackpad surface report here.
		raw_n = (NumBytesTransferred - headerSize) / fingerprintSize;
		if (raw_n >= PTP_MAX_CONTACT_POINTS) raw_n = PTP_MAX_CONTACT_POINTS;
		PtpReport.ContactCount = (UCHAR) raw_n;

#ifdef INPUT_CONTENT_TRACE
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! with %llu points.",
			raw_n
		);
#endif

		// Fingers
		for (i = 0; i < raw_n; i++) {

			UCHAR *f_base = Buffer + headerSize + DeviceContext->DeviceInfo->tp_delta;
			f = (const struct TRACKPAD_FINGER*) (f_base + i * fingerprintSize);

			// Translate X and Y
			x = (AmtRawToInteger(f->abs_x) - DeviceContext->DeviceInfo->x.min) > 0 ? 
				((USHORT)(AmtRawToInteger(f->abs_x) - DeviceContext->DeviceInfo->x.min)) : 0;
			y = (DeviceContext->DeviceInfo->y.max - AmtRawToInteger(f->abs_y)) > 0 ? 
				((USHORT)(DeviceContext->DeviceInfo->y.max - AmtRawToInteger(f->abs_y))) : 0;

			// Defuzz functions remain the same
			// TODO: Implement defuzz later
			PtpReport.Contacts[i].ContactID = (UCHAR) i;
			PtpReport.Contacts[i].X = x;
			PtpReport.Contacts[i].Y = y;
			PtpReport.Contacts[i].TipSwitch = (AmtRawToInteger(f->touch_major) << 1) >= 200;
			PtpReport.Contacts[i].Confidence = (AmtRawToInteger(f->touch_minor) << 1) > 0;

#ifdef INPUT_CONTENT_TRACE
			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_INPUT,
				"%!FUNC!: Point %llu, X = %d, Y = %d, TipSwitch = %d, Confidence = %d, tMajor = %d, tMinor = %d, origin = %d, PTP Origin = %d",
				i,
				PtpReport.Contacts[i].X,
				PtpReport.Contacts[i].Y,
				PtpReport.Contacts[i].TipSwitch,
				PtpReport.Contacts[i].Confidence,
				AmtRawToInteger(f->touch_major) << 1,
				AmtRawToInteger(f->touch_minor) << 1,
				AmtRawToInteger(f->origin),
				(UCHAR) i
			);
#endif
		}
	}

	// Type 2 touchpad contains integrated trackpad buttons
	if (DeviceContext->IsButtonReportOn) {
		// Handles trackpad button input here.
		if (Buffer[DeviceContext->DeviceInfo->tp_button]) {
			PtpReport.IsButtonClicked = TRUE;
		}
	}

	// Compose final report and write it back
	Status = WdfMemoryCopyFromBuffer(
		RequestMemory,
		0,
		(PVOID) &PtpReport,
		sizeof(PTP_REPORT)
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! WdfMemoryCopyFromBuffer failed with %!STATUS!",
			Status
		);
		goto exit;
	}

	// Set result
	WdfRequestSetInformation(
		Request,
		sizeof(PTP_REPORT)
	);

	// Set completion flag
	WdfRequestComplete(
		Request,
		Status
	);

exit:
	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);
	return Status;

}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpServiceTouchInputInterruptType5(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ UCHAR* Buffer,
    _In_ size_t NumBytesTransferred
)
{
    NTSTATUS Status;
    WDFREQUEST Request;
    WDFMEMORY RequestMemory;
    PTP_REPORT PtpReport = {0};
    PTP_CONTACT contactsById[MAX_FINGERS] = {0};
    unsigned int contactOrder[MAX_FINGERS] = {0};
    unsigned int physicalDown = 0;
    AMT_CONTACT_LIFECYCLE nextLifecycle;
    LARGE_INTEGER CurrentPerfCounter;
    LARGE_INTEGER PerfFrequency;
    LONGLONG PerfCounterDelta;
    size_t headerSize = (size_t)DeviceContext->DeviceInfo->tp_header;
    size_t fingerprintSize = (size_t)DeviceContext->DeviceInfo->tp_fsize;
    size_t raw_n;
    size_t i;

    // Validate before changing contact state or retrieving an output request.
    if (fingerprintSize < FSIZE_TYPE5 || NumBytesTransferred < headerSize ||
        (NumBytesTransferred - headerSize) % fingerprintSize != 0 ||
        DeviceContext->DeviceInfo->tp_delta != 0 ||
        (size_t)DeviceContext->DeviceInfo->tp_button >= headerSize) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    raw_n = (NumBytesTransferred - headerSize) / fingerprintSize;
    if (raw_n > MAX_FINGERS) return STATUS_DEVICE_DATA_ERROR;

    PtpReport.ReportID = REPORTID_MULTITOUCH;
    AmtEdgeBeginFrame(&DeviceContext->EdgeRejection);
    // Track ALL hardware contacts, even without a pending host read or when
    // surface reporting is disabled. Otherwise a held wrist can be reclassified.
    for (i = 0; i < raw_n; i++) {
        const UCHAR* finger = Buffer + headerSize + i * fingerprintSize;
        // Decode little-endian packed coordinates without unaligned word reads.
        UINT packed = (UINT)finger[0] | ((UINT)finger[1] << 8) |
            ((UINT)finger[2] << 16) | ((UINT)finger[3] << 24);
        INT x = (INT)(packed & 0x1fff);
        INT y = (INT)((packed >> 13) & 0x1fff);
        INT width = DeviceContext->DeviceInfo->x.max - DeviceContext->DeviceInfo->x.min;
        INT height = DeviceContext->DeviceInfo->y.max - DeviceContext->DeviceInfo->y.min;
        unsigned int contactId = finger[8] & 0x0f;
        // Area remains nonzero while a finger hovers near the surface.
        // Only the protocol's confirmed-contact state is an actual DOWN.
        int touching = (finger[3] & 0xc0) == 0x80;
        int confidence;
        if (x & 0x1000) x -= 0x2000;
        if (y & 0x1000) y -= 0x2000;
        // Preserve the upstream rounding without negating INT_MIN or shifting
        // negative signed integers. The low X bits form the fractional part.
        y = -y - ((packed & 0x1fff) != 0);
        x -= DeviceContext->DeviceInfo->x.min;
        y -= DeviceContext->DeviceInfo->y.min;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > width) x = width;
        if (y > height) y = height;

        confidence = AmtEdgeConfidence(&DeviceContext->EdgeRejection,
            contactId, x, y,
            width, height,
            // Area bytes can spike in hover/start/lift states (0x00/40/c0).
            // Only confirmed physical contact (0x80) contributes to the
            // consecutive-frame size test; edge-origin rejection is unchanged.
            touching, (finger[3] & 0xc0) != 0x80 ||
                ((unsigned int)finger[5] << 1) < 345);

        contactOrder[i] = contactId;
        if (touching) physicalDown |= 1u << contactId;
        contactsById[contactId].ContactID = contactId;
        contactsById[contactId].X = (USHORT)x;
        contactsById[contactId].Y = (USHORT)y;
        contactsById[contactId].TipSwitch = touching ? 1 : 0;
        contactsById[contactId].Confidence = confidence ? 1 : 0;
        if (DeviceContext->IsSurfaceReportOn && i < PTP_MAX_CONTACT_POINTS) {
            // Report accidental contacts using the PTP Confidence flag. Keep
            // contact IDs, count and UP events intact for Windows tracking.
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT,
                "MT2 contact slot=%llu id=%u x=%d y=%d tip=%d confidence=%d major=%u minor=%u state=%u",
                i, contactId, x, y, touching, confidence,
                (UINT)finger[4], (UINT)finger[5], (UINT)(finger[3] & 0xc0));
        }
    }
    AmtEdgeEndFrame(&DeviceContext->EdgeRejection);
    AmtContactObserve(&DeviceContext->ContactLifecycle, physicalDown);
    if (DeviceContext->IsButtonReportOn && Buffer[DeviceContext->DeviceInfo->tp_button]) {
        PtpReport.IsButtonClicked = TRUE;
    }

    Status = WdfIoQueueRetrieveNextRequest(DeviceContext->InputQueue, &Request);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT,
        "MT2 frame count=%llu active=%u rejected=%u read=%!STATUS!",
        raw_n, DeviceContext->EdgeRejection.Active,
        DeviceContext->EdgeRejection.Rejected, Status);
    if (!NT_SUCCESS(Status)) return Status;

    Status = WdfRequestRetrieveOutputMemory(Request, &RequestMemory);
    if (!NT_SUCCESS(Status)) {
        WdfRequestComplete(Request, Status);
        return Status;
    }

    nextLifecycle = DeviceContext->ContactLifecycle;
    AmtContactBuild(&nextLifecycle, contactsById, contactOrder, (unsigned int)raw_n,
        physicalDown, DeviceContext->IsSurfaceReportOn, &PtpReport);
    for (i = 0; i < PtpReport.ContactCount; ++i) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT,
            "MT2 output slot=%llu id=%u x=%u y=%u tip=%u confidence=%u",
            i, (UINT)PtpReport.Contacts[i].ContactID,
            (UINT)PtpReport.Contacts[i].X, (UINT)PtpReport.Contacts[i].Y,
            (UINT)PtpReport.Contacts[i].TipSwitch, (UINT)PtpReport.Contacts[i].Confidence);
    }
    QueryPerformanceCounter(&CurrentPerfCounter);
    QueryPerformanceFrequency(&PerfFrequency);
    PerfCounterDelta = CurrentPerfCounter.QuadPart - DeviceContext->PerfCounter.QuadPart;
    // PTP ScanTime is a rolling 16-bit clock in 100 us units. Splitting the
    // division avoids multiplying the full (potentially long) uptime by 10000.
    PtpReport.ScanTime = (USHORT)(
        (PerfCounterDelta / PerfFrequency.QuadPart) * 10000 +
        (PerfCounterDelta % PerfFrequency.QuadPart) * 10000 / PerfFrequency.QuadPart);

    Status = WdfMemoryCopyFromBuffer(RequestMemory, 0, &PtpReport, sizeof(PtpReport));
    if (NT_SUCCESS(Status)) {
        AmtContactCommit(&nextLifecycle, &PtpReport);
        DeviceContext->ContactLifecycle = nextLifecycle;
        WdfRequestSetInformation(Request, sizeof(PtpReport));
    }
    WdfRequestComplete(Request, Status);
    return Status;
}

// Helper function for numberic operation
static inline INT AmtRawToInteger(
	_In_ USHORT x
)
{
	return (signed short) x;
}
