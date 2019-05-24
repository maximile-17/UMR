# README file for code
every folder has its own README, this file contains all readme information.
 -----------------------------------------------------


# README for inter-intra_manual_pack
--------
    test manual copy performance of non-contiguous data communication in inter-node and intra-node
        (share memory or not inside node; inter-node RoCE)
----------------------------------------------------------------------

# README for fft2d_DDT_mxx
--------
    test performance of different ways to accomplish 2DFFT 
        manual pack/unpack----No ddt
        send ddt
        recv ddt
        send and recv ddt
----------------------------------------------------------------------

# README  for sgrs_IE
--------
    simple test for performance of SGRS and manual pack/unpack
        just a try to use sg_list
---------------------------------------------------------------------- 

# README for copy_dma
--------
    test copy overhead and nic communication overhead seperately
        used for analysing the relationship between data size and communication performance.  
 ----------------------------------------------------------------------

## README for sgrs_umr_IE
------------------
    compare performance between sgrs，umr(send/recv and write)，memory copy。
    data layout in sender and receiver are symmetrical
-------------------------------------------------------------------

# README for 2DFFT
--------
    test performance of different ways to accomplish 2DFFT 
        manual pack/unpack----No ddt
        send ddt
        recv ddt
        send and recv ddt
        sgrs(using sg_list) :this test can't support large matrix size 2DFFT computing
----------------------------------------------------------------------

# README for UMR-master
-----------------------------------------------------
    compare performance（latency） between sgrs，umr_send-recv，umr_write，memory copy

    --------------------------------------------------------------
    # sgrs_umr: 
    symmetrical data layout in both sides

    simple copy(buf_cp copied to buf_sg, one sge, one mr);
    sg_list(buf_sg, block_num sg entries, one mr)
    umr(buf_umr, one sge, block_num mr->UMR)
    umr_write(the same as umr) 

    --------------------------------------------------------------
    # sgrs_umr_asym_try: 
    test for umr whether support different data layouts in sender and receiver

    --------------------------------------------------------------
    # sgrs_umr_asym:
    test for performance（latency） between sgrs，umr_send-recv，umr_write，memory copy when data layout in both sides is symmetrical 

    --------------------------------------------------------------
    # sgrs_umr_length:
    make sure the maining of umr.length
    create umr.length=each mr's data length, not mr-length

    --------------------------------------------------------------
    # sgrs_umr_largelist:
    for large number of block_num test, sg_list needs many sg entries, umr needs only one sg entry. compare both performances

    --------------------------------------------------------------
    # sgrs_umr_nmr:
    SGRS use the same number of mr as umr, not one mr

    --------------------------------------------------------------
    # sgrs_stencil_w:
    test sgrs write + manual copy performance in stencil communication micro-application

    --------------------------------------------------------------
    # sgrs_stencil_sr:
    test sgrs(send/recv) performance in stencil communication micro-application

    --------------------------------------------------------------
    # umr_stencil:
    test UMR performance in stencil communication micro-application

    --------------------------------------------------------------
    # nas_mg_z_sr:
    test UMR(send/recv and write) sgrs(send/recv) performance in nas_mg_z micro-application

    --------------------------------------------------------------
    # nas_mg_z_w:
    test UMR(send/recv and write) sgrs(write + manual pack/unpack) performance in nas_mg_z micro-application

    --------------------------------------------------------------
    # sgrs_umr_cache:
    1 UMR contains different mr layouts, compare performace between sgrs and umr 
----------------------------------------------------------------