import re
import sh

BIN="/dev/shm/libnvwal/build/experiments/lsn_microbench"
DISK_DIR="/dev/shm/libnvwal/build/microbench/disk_root"
NVM_DIR="/dev/shm/libnvwal/build/microbench/nv_root"

def run_lsn_workload(nops, segment_size, nlogrec, logrec_size):
  run = sh.Command(BIN)
  nv_quota = segment_size * 8
  out = run("-disk_folder", DISK_DIR, 
            "-nvm_folder", NVM_DIR, 
            "-nops", nops, 
            "-segment_size", segment_size, 
            "-nv_quota", nv_quota, 
            "-nlogrec", nlogrec, 
            "-logrec_size", logrec_size)

  for l in out.split('\n'):
    if re.search("workload_duration", l):
      m = re.match("workload_duration (.+) us", l)
      duration = float(m.group(1))
      return duration
  

#for segment_size_pow in range(9, 20):
for segment_size_pow in range(18, 19):
  nops = 1000000
  segment_size = 2 ** segment_size_pow
  nlogrec = 8
  logrec_size = 128
  duration_us = run_lsn_workload(nops, segment_size, nlogrec, logrec_size)
  total_bytes = nops * nlogrec * logrec_size
  throughput = total_bytes * 1000000 / duration_us 
  print segment_size, throughput
