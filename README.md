# sgrs_umr-bench
compare symmetrical vector datatype performance（latency） between sgrs，umr_send-recv，umr_write，memory copy。
# sgrs_umr: 
    simple copy(buf_cp copied to buf_sg, one sge, one mr);
    sg_list(buf_sg, block_num sg entries, one mr)
    umr(buf_umr, one sge, block_num mr->UMR)
    umr_write(the same as umr) 
# sgrs_umr_asym: 
    test for umr whether support different data layouts in sender and receiver
# sgrs_umr_length:
    create umr.length=each mr's data length, not mr-length
# sgrs_umr_largelist:
    for large number of block_num test, sg_list needs many sg entries, umr needs only one sg entry. compare both performances
# sgrs_umr_nmr:
    SGRS use the same number of mr as umr, not one mr
