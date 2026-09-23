"""Compile the actual Type 5 handler with mocked WDF, and run contact scenarios.

Requires Python 3 and a C compiler (CC defaults to cc). This is not a WDK build.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--msvc", action="store_true", help="Use cl.exe from a VS developer shell")
parser.add_argument("--trace", type=Path, help="Replay local contact JSON produced by analyze-scroll-trace.py")
options = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/AmtPtpDeviceUsbUm/InputInterrupt.c").read_text()
start = source.index("NTSTATUS\nAmtPtpServiceTouchInputInterruptType5(")
end = source.index("// Helper function for numberic operation", start)
hid = (root / "src/AmtPtpDeviceUsbUm/include/Hid.h").read_text()
hid_start = hid.index("#pragma pack(1)\ntypedef struct _PTP_CONTACT")
hid_end = hid.index("typedef struct _PTP_USERMODEAPP_CONF_REPORT", hid_start)
with tempfile.TemporaryDirectory(prefix="amt-edge-tests-") as temporary:
    work = Path(temporary)
    (work / "type5_handler.inc").write_text(source[start:end])
    (work / "ptp_report.inc").write_text(hid[hid_start:hid_end])
    replay = ["static void replay_trace(void) {"]
    if options.trace:
        frames = json.loads(options.trace.read_text())
        rows = []
        original_bad = 0
        for index, entry in enumerate(frames):
            # Replay frames with complete per-contact diagnostics only.
            if len(entry["contacts"]) != entry["count"]:
                raise ValueError("Incomplete trace frame")
            if sum(c["tip"] for c in entry["contacts"]) == 2:
                original_bad += any(c["tip"] and not c["confidence"] for c in entry["contacts"])
            for c in entry["contacts"]:
                rows.append("{%d,%d,%d,%d,%d,%d,%d,%d}" % (
                    index, c["slot"], c["id"], c["x"], c["y"], c["major"], c["minor"], c["state"]))
        replay += ["static const int rows[][8] = {" + ",\n".join(rows) + "};",
                   "static const int counts[] = {" + ",".join(str(f["count"]) for f in frames) + "};",
                   "size_t r=0; unsigned int rejected=0, two=0, checkedDown=0, checkedUp=0; PTP_REPORT previous={0}; reset();",
                   "for (int f=0; f<(int)(sizeof(counts)/sizeof(counts[0])); ++f) {",
                   "  memset(packet,0,sizeof(packet));",
                   "  while(r<sizeof(rows)/sizeof(rows[0]) && rows[r][0]==f) {",
                   "    const int* c=rows[r++]; finger(c[1],(unsigned int)c[2],c[3],c[4],c[5]!=0,c[6]);",
                   "    packet[12+9*c[1]+4]=(UCHAR)c[5];",
                   "    packet[12+9*c[1]+3]=(UCHAR)((packet[12+9*c[1]+3]&0x3f)|c[7]);",
                   "  }",
                   "  assert(frame(counts[f])==0); verify();",
                   "  unsigned int ids=0; assert(output.ContactCount<=5);",
                   "  int tips=0,bad=0; for(int s=0;s<output.ContactCount;++s) {",
                   "    tips+=output.Contacts[s].TipSwitch;",
                   "    bad|=output.Contacts[s].TipSwitch && !output.Contacts[s].Confidence;",
                   "    const PTP_CONTACT* c=&output.Contacts[s]; assert(c->ContactID<16);",
                   "    assert(!(ids & (1u<<c->ContactID))); ids|=1u<<c->ContactID;",
                   "    if(c->TipSwitch) { int found=0;",
                   "      for(int k=0;k<counts[f];++k) {const UCHAR* p=packet+12+9*k; if(((unsigned int)p[8]&15u)==c->ContactID && (p[3]&0xc0)==0x80) found=1;}",
                   "      assert(found); ++checkedDown;",
                   "    } else { int found=0;",
                   "      for(int k=0;k<previous.ContactCount;++k) {const PTP_CONTACT* p=&previous.Contacts[k]; if(p->ContactID==c->ContactID && p->TipSwitch){assert(c->X==p->X && c->Y==p->Y);found=1;}}",
                   "      assert(found); ++checkedUp;",
                   "    }",
                   "  }",
                   "  if(tips==2) {++two; rejected+=bad!=0;} previous=output;", "}",
                   f'printf("REPLAY: %u two-touch frames, rejected %u (recorded baseline {original_bad}); %u confirmed DOWNs, %u stable UPs, no hover DOWNs\\n",two,rejected,checkedDown,checkedUp);']
    replay += ["}"]
    (work / "replay_trace.inc").write_text("\n".join(replay))
    for percent in (5, 0):
        binary = work / (f"edge-tests-{percent}.exe" if options.msvc else f"edge-tests-{percent}")
        if options.msvc:
            command = [
                "cl.exe", "/nologo", "/std:c11", "/W4", "/WX",
                f"/DAMT_PTP_EDGE_PERCENT={percent}", "/I", str(work),
                "/I", str(root / "src/AmtPtpDeviceUsbUm/include"),
                str(root / "tests/edge_rejection.c"),
                f"/Fe{binary}", f"/Fo{work / 'edge-tests.obj'}"
            ]
        else:
            command = [
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                f"-DAMT_PTP_EDGE_PERCENT={percent}", "-I", str(work),
                "-I", str(root / "src/AmtPtpDeviceUsbUm/include"),
                str(root / "tests/edge_rejection.c"), "-o", str(binary)
            ]
        subprocess.run(command, check=True, cwd=work)
        subprocess.run([str(binary)], check=True)
