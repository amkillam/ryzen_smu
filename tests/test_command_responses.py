"""Compile the actual driver's command function against simulated SMN I/O."""
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

source = (Path(__file__).resolve().parents[1] / 'smu.c').read_text()
function = source[source.index('enum smu_return_val smu_send_command('):source.index('int smu_resolve_cpu_class(')]
header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
struct pci_dev { int unused; };
enum smu_mailbox { MAILBOX_TYPE_RSMU, MAILBOX_TYPE_MP1, MAILBOX_TYPE_HSMP };
enum smu_return_val { SMU_Return_OK=1, SMU_Return_Failed=255,
 SMU_Return_CommandTimeout=251, SMU_Return_Unsupported=249, SMU_Return_PCIFailed=246 };
#define SMU_REQ_MAX_ARGS 6
typedef union { u32 args[6]; struct {u32 arg0,arg1,arg2,arg3,arg4,arg5;} s; } smu_req_args_t;
static struct {u32 addr_rsmu_mb_rsp,addr_rsmu_mb_cmd,addr_rsmu_mb_args,
 addr_mp1_mb_rsp,addr_mp1_mb_cmd,addr_mp1_mb_args,
 addr_hsmp_mb_rsp,addr_hsmp_mb_cmd,addr_hsmp_mb_args;} g_smu={1,2,10,1,2,10,1,2,10};
static u32 smu_timeout_attempts=3;
static int amd_smu_mutex, lock_count, initial_busy, post_busy, sent, writes, status, read_fail;
#define pr_debug(...) ((void)0)
#define pr_warn(...) ((void)0)
static void mutex_lock(int *m) {(void)m; lock_count++;}
static void mutex_unlock(int *m) {(void)m; lock_count--;}
static enum smu_return_val smu_read_address(struct pci_dev *dev,u32 addr,u32 *value) {
 (void)dev;
 if(read_fail) return SMU_Return_PCIFailed;
 if(addr != 1) {*value=123; return SMU_Return_OK;}
 if(!sent) { *value=initial_busy-- > 0 ? 0 : 1; }
 else { *value=post_busy-- > 0 ? 0 : status; }
 return SMU_Return_OK;
}
static enum smu_return_val smu_write_address(struct pci_dev *dev,u32 addr,u32 value) {
 (void)dev;(void)value; writes++; if(addr==2) sent=1; return SMU_Return_OK;
}
'''
tail = r'''
static void run(const char *name,int busy_before,int busy_after,int rsp,int pci_fail,int expected,int command_sent) {
 initial_busy=busy_before;post_busy=busy_after;status=rsp;read_fail=pci_fail;
 sent=writes=lock_count=0;
 smu_req_args_t args={0};
 int result=smu_send_command(NULL,0x5E,&args,MAILBOX_TYPE_RSMU);
 assert(result==expected);assert(sent==command_sent);assert(lock_count==0);
 if(!command_sent) assert(writes==0);
 printf("PASS %s\n",name);
}
int main(void) {
 run("success",0,0,1,0,1,1);
 run("fast reject FF",0,0,255,0,255,1);
 run("unknown command FE",0,0,254,0,254,1);
 run("prerequisite rejection FD",0,0,253,0,253,1);
 run("initial busy timeout issues no command",20,0,1,0,251,0);
 run("command timeout",0,20,1,0,251,1);
 run("fresh timeout budget after initial wait",2,2,1,0,1,1);
 run("PCI read failure releases lock",0,0,1,1,246,0);
 return 0;
}
'''
with TemporaryDirectory(prefix='ryzen-smu-response-test-') as temporary:
    base = Path(temporary)
    output = base / 'driver-response-harness.c'
    binary = base / 'driver-response-harness'
    output.write_text(header + function + tail)
    subprocess.run(['cc', '-Wall', '-Wextra', '-O2', str(output), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
