#!/bin/bash

#This script is used to run verbs through Openmpi
#environment in suse108 & suse109 --2019.04.01
set -euxo pipefail
OMPI_PATH=/home/mxx/opt/openmpi-4.0.0
HOSTFILE=/home/mxx/HOSTS/ompi_hosts
IB_SUPPROT="btl openib,self,vader"
IB_HCA="btl_openib_if_include mlx5_0"
RECEIVE_QUEUES="btl_openib_receive_queues P,128,256,192,128:S,2048,256,128,32:S,12288,256,128,32:S,65536,256,128,32"
VERBOSE="btl_base_verbose 50"
BIND="--report-bindings"
EXC1="coll ^hcoll"
EXC2="osc ^ucx"
EXC3="pml ^ucx"
ROOT="--allow-run-as-root"
NODE="--map-by node"
SGRS_UMR=/home/mxx/ddt_direct/UMR-master/sgrs_umr

scp /home/mxx/ddt_direct/UMR-master/sgrs_umr* suse108:/home/mxx/ddt_direct/UMR-master/

#for block_size in 128 256 512 1024 2048 4096 8192 16384 32768 65536 $[65536*2] $[65536*4] $[1024*512] $[1024*1024] $[1024*1024*2] $[1024*1024*4] $[1024*1024*8] $[1024*1024*16]
#do
#$OMPI_PATH/bin/mpirun -np 2 -hostfile $HOSTFILE $NODE $BIND $ROOT $1 -E 1 -b $block_size -n 16 -s $[1024*1024*16] -W 100 -N 1000
#done

for block_num in 8 16 32  
do
  for block_size in 128 256 512 1024 2048 4096 8192 16384 32768 65536 $[65536*2] $[65536*4] $[1024*512] $[1024*1024] $[1024*1024*2] $[1024*1024*4] 
  do 
  $OMPI_PATH/bin/mpirun -np 2 -hostfile $HOSTFILE $NODE $BIND $ROOT $1 -E 1 -b $block_size -n $block_num -s $[1024*1024*8] -W 1000 -N 10000
  done
done
#$OMPI_PATH/bin/mpirun -np 2 -hostfile $HOSTFILE --mca $IB_SUPPROT \
#                                          --mca $IB_HCA\
#                                          --mca $RECEIVE_QUEUES\
#                                          --mca $VERBOSE\
#                                        $BIND $ROOT $1

