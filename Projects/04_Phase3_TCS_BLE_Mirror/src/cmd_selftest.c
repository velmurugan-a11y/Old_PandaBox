/*
 * cmd_selftest.c -- boot-time validation of cmd.c's non-meter Lx command
 * responses, run entirely on real hardware since this project has no
 * native compiler available to unit-test cmd.c off-target. Exercises the
 * exact compiled code path (including newlib-nano's real snprintf/sscanf
 * behavior on this target) rather than a native x86 recompilation that
 * could subtly diverge. Runs once at boot, before yc1021_init(), so it
 * doesn't depend on BLE being up at all.
 */
#include "cmd_selftest.h"
#include "cmd.h"
#include "log.h"
#include <string.h>

static void run(const char *in)
{
    char out[200];
    uint32_t n = cmd_process_line(in, (uint32_t)strlen(in), out, sizeof(out));

    log_line("SELFTEST IN:  ");
    log_line(in);
    log_line("\r\nSELFTEST OUT: ");
    if (n == 0u) {
        log_line("(no reply)\r\n");
    } else {
        log_line(out); /* already ends in \r\n */
    }
}

void cmd_selftest_run(void)
{
    log_line("\r\n=== cmd.c self-test: non-meter Lx commands ===\r\n");

    /* This test exercises every Set* command, including with throwaway
     * values (e.g. SetBtName MyTestBox) -- must not let those overwrite
     * the real persisted settings on flash. Re-enabled below before the
     * final restore cmd_init(), which then does a normal (harmless)
     * reload of whatever is actually persisted. */
    cmd_set_persist_enabled(0);
    cmd_init();

    run("BoxInfo,");
    run("BoxStatus,");
    run("BoxTime,");
    run("RdBtName,");
    run("SetBtName MyTestBox,");
    run("RdBtName,");                 /* should now echo MyTestBox */

    /* -------- Meter control + GetData, deliberately BEFORE SetPortLcrNode --------
     * Phase 3 made Start/Pause/Resume/Stop/Print real (blocking, real
     * RS485 bus I/O with a multi-second timeout) whenever a node IS
     * assigned -- see tcs.c. Running these here, before any node is
     * assigned, exercises the fast "no node -> immediate failure, no bus
     * I/O attempted" path instead, which is what should always happen
     * with no meter physically attached -- and keeps this self-test fast
     * on every boot instead of adding several multi-second bus timeouts.
     * The real bus success path can only be meaningfully tested against
     * an actual meter anyway (see the port plan's Phase 3 verification),
     * not blind in a self-test. GetData's live path (flag=0) similarly
     * falls back to its mock path with no node assigned -- exercised here
     * too. */
    run("Start 1,");                   /* no node yet -- must fail (1), no bus I/O attempted */
    run("GetLastMtrCmd 1,");           /* should still show None,1 (Start early-returned before touching state) */
    run("Pause 1,");                   /* no node -- fail (1) */
    run("Resume 1,");                  /* no node -- fail (1) */
    run("Stop 1,");                    /* no node -- fail (1) */
    run("Print 1,");                   /* no node -- fail (1) */
    run("PresetGross 1,500");          /* PresetGross/Net don't check node -- RAM store, always succeeds */
    run("PresetNet 1,480");
    run("Start 3,");                   /* invalid meter index -- must fail (1) */

    run("GetData 1,0,");               /* no node -- mock fallback, live reading */
    run("GetData 1,0,");               /* serial should advance */
    run("GetData 1,1,");               /* history path -> real flash_store read (empty so far) or "All Read" */
    run("GetDataTs 1,1700000000,1700003600,");
    run("GetDataEcho 1,1,2,");
    run("HisDataTime 1,");

    run("SetPortLcrNode 1,2");
    run("RdPortLcrNode,");
    run("RdRegister,");
    run("GetLastCmd,");                /* should echo "RdRegister", not itself */
    run("GetLastMtrCmd 1,");
    run("BoxStorage 1,");
    run("DeleteAll 1,");

    /* -------- Config (RAM only) -------- */
    run("SetMode 2,");
    run("SetBtPwd 1234,");
    run("SetWifiName MyWifi,");
    run("SetWifiPwd MyWifiPwd,");
    run("SetServerIp 192.168.1.1,");
    run("SetServerPort 8080,");
    run("SetApn cmnet,");
    run("SetBoxTime 1700000000,");
    run("SetRs485 1,");                /* no reply expected */
    run("SetApp1,");                   /* no reply expected */

    /* -------- Node / diagnostics -------- */
    run("ModifyLcrNode 1,1,5,");
    run("RdPortLcrNode,");             /* port 1 node should now read 5 */
    run("GetLcrNode 1,1,10,");
    run("SwitchState 1,");
    run("RdDiagnostics 1,");
    run("Update app1 1024 ABCD,");     /* explicitly rejected -- OTA not implemented */

    run("SetDbg 0,");                  /* no reply expected */
    run("SetDbg 1,");                  /* no reply expected */
    run("TotallyUnknownCommand,");     /* no reply expected */
    run("DirectDelivery,");            /* undocumented command -- no reply expected */

    /* BoxReset is deliberately NOT exercised here -- it sets
     * cmd_reset_pending() and main.c will actually reset the MCU on the
     * next loop iteration, which would cut this self-test short. Verified
     * by code review instead: see cmd.c's BoxReset handler. */

    log_line("=== cmd.c self-test done ===\r\n\r\n");

    cmd_set_persist_enabled(1);
    cmd_init(); /* restore clean state (reloads real persisted settings) before the real BLE dispatcher runs */
}
