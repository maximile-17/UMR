/*
 * simple bench with 2 procs: 
 * Send Gather Receive Scatter
 * Memcory Copy
 * User-model Memory Registration
 */

/*
 * data memset, data check, sg_list had been changed
 * SR_COPY use buf_sg mr
 * SGRS use buf_umr emr
 * UMR and UMR_W use the same buf as SGRS
 */

#include "sgrs_umr.h"

int main(int argc, char **argv)
{
	int myrank, numprocs;
	int opt;
	

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

	struct params parameters;
	parameters.block_size =  BLOCK_SZ;
 	parameters.block_num = BLOCK_N;
	parameters.stride = STRIDE;
  	parameters.iterW = WITER;
  	parameters.iterN = NITER;
	parameters.IBlink = 0;
	parameters.Ethlink = 1;  
	parameters.mtu = 1024;
	parameters.dbg = 0;
	int sglist_num = 1;
	int last_sge_num = 0;
	int mr_num = 1;
	bool dbg = 0;
	if (parser(argc, argv, &parameters, numprocs, myrank)) {
		if (0 == myrank) {
			fprintf(stderr, "Parser parameters error!\n");
		}
		goto EXIT_MPI_FINALIZE;
	}
	dbg = parameters.dbg;
	//mr
	int imr;
	mr_num = parameters.block_num;
	struct ibv_mr *emr[mr_num];
	struct ibv_exp_create_mr_in  create_mr_in;
	// initialize random number generator
	srand48(getpid()*time(NULL));

	// find IB devices
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		fprintf(stderr, "failed to get device list!\n");
		goto EXIT_MPI_FINALIZE;
	}

	// pick the HCA
	dev = dev_list[IB_DEV];
	dev_ctx = ibv_open_device(dev);
	if (!dev_ctx) {
		fprintf(stderr, "failed to open device!\n");
		goto EXIT_FREE_DEV_LIST;
	}
	if (ibv_query_device(dev_ctx, &dev_attr)) {
		fprintf(stderr, "failed to query device!\n");
		goto EXIT_CLOSE_DEV;
	}

	//////////////////////////////////////////////////////////
	//query and print device umr support info
	if (ibv_exp_query_device(dev_ctx, &edev_attr)) {
		fprintf(stderr, "failed to query device!\n");
		goto EXIT_CLOSE_DEV;
	}
	//ibv_exp_device_cap_flags
	edev_attr.comp_mask |= IBV_EXP_DEVICE_ATTR_UMR;
	if (ibv_exp_query_device(dev_ctx, &edev_attr)) 
		printf("myrank:%d can't query for UMR capabilities, my device exp_device_cap_flags is:%o.\n", myrank, edev_attr.exp_device_cap_flags);
	if(myrank){
		printf("\n==========Device info & UMR capbilities info:==========\nmax_klm_list_size:%13d; \nmax_send_wqe_inline_klms:%6d; \nmax_umr_recursion_depth:%6d.\n",\
		edev_attr.umr_caps.max_klm_list_size,edev_attr.umr_caps.max_send_wqe_inline_klms,edev_attr.umr_caps.max_umr_recursion_depth);
	}
	/////////////////////////////////////////////////////////////

	//compare block number with max_sge
	if (parameters.block_num > dev_attr.max_sge) {
		sglist_num = mr_num / dev_attr.max_sge;
		last_sge_num = mr_num % dev_attr.max_sge;
		if(last_sge_num != 0) sglist_num++;
		if(myrank == 0)
			printf("block_num is bigger than the device's max_sge, \nSGRS needs %d WR with max_age(%d), another WR with %d sg entries.\n"\
		,sglist_num-1, dev_attr.max_sge, last_sge_num);
	}

	struct ibv_send_wr      nsr[sglist_num];
	struct ibv_recv_wr      nrr[sglist_num];

	// check the port
	if (ibv_query_port(dev_ctx, IB_PORT, &port_attr)) {
		fprintf(stderr, "failed to query port!\n");
		goto EXIT_CLOSE_DEV;
	}
	if (port_attr.state != IBV_PORT_ACTIVE) {
		fprintf(stderr, "port not ready!\n");
		goto EXIT_CLOSE_DEV;
	}

	// print some device info
	if(myrank)
		printf("rank%d | device: %s | port: %d | lid: %u | max sge: %d | max mr:%d | max cqe:%d | max qp wr:%d\n",
	        myrank, ibv_get_device_name(dev), IB_PORT, port_attr.lid, dev_attr.max_sge, dev_attr.max_mr, dev_attr.max_cqe, dev_attr.max_qp_wr);

	// allocate protection domain
	pd = ibv_alloc_pd(dev_ctx);
	if (!pd) {
		fprintf(stderr, "failed to allocate protection domain!\n");
		goto EXIT_CLOSE_DEV;
	}

	// allocate memory buffers
	//buf_size = parameters.stride * dev_attr.max_sge;
	buf_size = parameters.stride * parameters.block_num;
	if (posix_memalign(&buf_sg, sysconf(_SC_PAGESIZE), buf_size)) {
		fprintf(stderr, "failed to allocate S/G buffer!\n");
		goto EXIT_DEALLOC_PD;
	}
	if (posix_memalign(&buf_cp, sysconf(_SC_PAGESIZE), buf_size)) {
		fprintf(stderr, "failed to allocate copy buffer!\n");
		goto EXIT_FREE_BUF;
	}
	if (posix_memalign(&buf_umr, sysconf(_SC_PAGESIZE), buf_size)) {
		fprintf(stderr, "failed to allocate umr buffer!\n");
		goto EXIT_FREE_BUF;
	}

	// register the S/G buffer for RDMA
	mr = ibv_reg_mr(pd, buf_sg, buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!mr) {
		fprintf(stderr, "failed to set up MR!\n");
		goto EXIT_FREE_BUF;
	}

	// create block_num mrs
	// TODO: compute mr params;  comfirm imr number
	for(imr=0; imr<mr_num; imr++){
		emr[imr] = ibv_reg_mr(pd, (void *)((uint64_t)(uintptr_t)buf_umr + imr * parameters.stride), parameters.block_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
		if (!emr[imr]) {
		fprintf(stderr, "failed to set up %dst MR for UMR!\n", imr);
		goto EXIT_DEREG_MR;
		}
	} 

	// create the completion queue
	cq = ibv_create_cq(dev_ctx, CQ_DEPTH, NULL, NULL, 0);
	if (!cq) {
		fprintf(stderr, "failed to create receive CQ!\n");
		goto EXIT_DEREG_EMR;
	}
	// create the exp completion queue
	ecq = ibv_create_cq(dev_ctx, CQ_DEPTH, NULL, NULL, 0);
	if (!ecq) {
		fprintf(stderr, "failed to create receive CQ!\n");
		goto EXIT_DESTROY_CQ;
	}

	// create the queue pair
	qp_init_attr.qp_type             = IBV_QPT_RC;
	qp_init_attr.send_cq             = cq;    // NULL would cause segfault ...
	qp_init_attr.recv_cq             = cq;
	qp_init_attr.cap.max_send_wr     = CQ_DEPTH;
	qp_init_attr.cap.max_recv_wr     = CQ_DEPTH;
	qp_init_attr.cap.max_send_sge    = dev_attr.max_sge;
	qp_init_attr.cap.max_recv_sge    = dev_attr.max_sge;
	qp = ibv_create_qp(pd, &qp_init_attr);
	if (!qp) {
		fprintf(stderr, "failed to create QP!\n");
		goto EXIT_DESTROY_ECQ;
	}
	
	// set QP status to INIT
	qp_attr.qp_state        = IBV_QPS_INIT;
	qp_attr.pkey_index      = 0;
	qp_attr.port_num        = IB_PORT;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
	if (ibv_modify_qp(qp, &qp_attr,
	                       IBV_QP_STATE      |
	                       IBV_QP_PKEY_INDEX |
	                       IBV_QP_PORT       |
	                       IBV_QP_ACCESS_FLAGS)) {
		fprintf(stderr, "failed to modify qp state to INIT!\n");
		goto EXIT_DESTROY_QP;
	};
	////////////////////////exp/////////////////////
	// create modify mr
	// TODO: add params;  comfirm imr number
	memset(&create_mr_in, 0, sizeof(create_mr_in));
	create_mr_in.pd = pd;
	create_mr_in.attr.max_klm_list_size = mr_num;//max number of entries
	//IBV_EXP_MR_SIGNATURE_EN; IBV_EXP_MR_INDIRECT_KLMS; IBV_EXP_MR_FIXED_BUFFER_SIZE;
	create_mr_in.attr.create_flags = IBV_EXP_MR_INDIRECT_KLMS;
	create_mr_in.attr.exp_access_flags = IBV_EXP_ACCESS_LOCAL_WRITE | IBV_EXP_ACCESS_REMOTE_WRITE;
	modify_mr = ibv_exp_create_mr(&create_mr_in);
	if (!modify_mr) {
		fprintf(stderr, "failed to set up modify_mr for UMR!\n");
//		goto EXIT_FREE_DM;
		goto EXIT_DESTROY_QP;
	}
	
	//memkey_list container
	memset(&mem_objects_attr, 0, sizeof(mem_objects_attr));
	mem_objects_attr.pd = pd;
	mem_objects_attr.mkey_list_type = IBV_EXP_MKEY_LIST_TYPE_INDIRECT_MR;
	mem_objects_attr.max_klm_list_size = edev_attr.umr_caps.max_klm_list_size;
	mem_objects = ibv_exp_alloc_mkey_list_memory(&mem_objects_attr);
	if(!mem_objects){
		fprintf(stderr, "failed to allocate mkey_list_memory!\n");
		goto EXIT_DESTROY_MODIFYMR;
	}


	// create the exp_qp
	//TODO: comfirm what comp_mask used for 
	eqp_init_attr.qp_type             = IBV_QPT_RC;
	eqp_init_attr.pd                  = pd;
	eqp_init_attr.send_cq             = ecq;    // NULL would cause segfault ...
	eqp_init_attr.recv_cq             = ecq;
	eqp_init_attr.cap.max_send_wr     = CQ_DEPTH;
	eqp_init_attr.cap.max_recv_wr     = CQ_DEPTH;
	eqp_init_attr.cap.max_send_sge    = dev_attr.max_sge;
	eqp_init_attr.cap.max_recv_sge    = dev_attr.max_sge;
	//enable UMR and set params
	eqp_init_attr.exp_create_flags	 = IBV_EXP_QP_CREATE_UMR;
	eqp_init_attr.comp_mask		 = IBV_EXP_QP_INIT_ATTR_PD | IBV_EXP_QP_INIT_ATTR_MAX_INL_KLMS | IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS;
	eqp_init_attr.max_inl_send_klms	  = edev_attr.umr_caps.max_send_wqe_inline_klms;
	eqp = ibv_exp_create_qp(dev_ctx, &eqp_init_attr);
	if (!eqp) {
		fprintf(stderr, "failed to create EQP!\n");
		goto EXIT_DEALLOC_MKEY_LIST;
	}
		
	// set EQP status to INIT	
	eqp_attr.qp_state        = IBV_QPS_INIT;
	eqp_attr.pkey_index      = 0;
	eqp_attr.port_num        = IB_PORT;
	eqp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
	if (ibv_exp_modify_qp(eqp, &eqp_attr, IBV_EXP_QP_STATE | IBV_EXP_QP_PKEY_INDEX | IBV_EXP_QP_PORT | IBV_EXP_QP_ACCESS_FLAGS)) {
		fprintf(stderr, "failed to modify eqp state to INIT!\n");
		goto EXIT_DESTROY_EQP;
	}

	// exchange connection info
	local_conn  = (const struct ib_conn) { 0 };
	remote_conn = (const struct ib_conn) { 0 };
	local_conn.lid = port_attr.lid;
	local_conn.qpn = qp->qp_num;
	local_conn.psn = lrand48() & 0xffffff;
	local_conn.myid = myrank;
  	/*message for RDMA write: address and rkey*/
  	local_conn.addr = (uintptr_t)buf_umr;
  	local_conn.rkey = modify_mr->rkey;
	//ethernet link must have gid
	if(parameters.Ethlink){
		if(ibv_query_gid(dev_ctx, 1, 0, &mygid)){
			fprintf(stderr,"failed to get gid for port 1 , index 0!\n");
		}
		memcpy(local_conn.gid, &mygid, sizeof(mygid));
	}

	MPI_Sendrecv(&local_conn, sizeof(struct ib_conn), MPI_CHAR, myrank ? 0 : 1, myrank ? 1 : 0, &remote_conn, sizeof(struct ib_conn), MPI_CHAR, myrank ? 0 : 1, myrank ? 0 : 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	printf("\n===================exchange info for rdma qp===================\nrank%d_local : LID %#04x, QPN %#06x, PSN %#06x, ID %d, umr_addr %x, umr_rkey %d.\nrank%d_remote: LID %#04x, QPN %#06x, PSN %#06x ,ID %d, umr_addr %x, umr_rkey %d.\n",
	      myrank,  local_conn.lid,  local_conn.qpn,  local_conn.psn, local_conn.myid, local_conn.addr, local_conn.rkey,\
	      myrank, remote_conn.lid, remote_conn.qpn, remote_conn.psn, remote_conn.myid, remote_conn.addr, remote_conn.rkey);
	
	if(parameters.Ethlink){
		printf("\nRank%d  local_gid: %#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x;\nRank%d remote_gid: %#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x:%#2x%#2x\n",\
		myrank, local_conn.gid[0], local_conn.gid[1], local_conn.gid[2],local_conn.gid[3],local_conn.gid[4],\
		local_conn.gid[5],local_conn.gid[6],local_conn.gid[7],local_conn.gid[8],local_conn.gid[9],\
		local_conn.gid[10],local_conn.gid[11],local_conn.gid[12],local_conn.gid[13],local_conn.gid[14],	local_conn.gid[15],myrank,\
		remote_conn.gid[0], remote_conn.gid[1], remote_conn.gid[2],remote_conn.gid[3],remote_conn.gid[4],\
		remote_conn.gid[5],remote_conn.gid[6],remote_conn.gid[7],remote_conn.gid[8],remote_conn.gid[9],\
		remote_conn.gid[10],remote_conn.gid[11],remote_conn.gid[12],remote_conn.gid[13],remote_conn.gid[14], remote_conn.gid[15]);
	}
	
	// sender + receiver: set QP status to RTR
	qp_attr.qp_state              = IBV_QPS_RTR;
	qp_attr.path_mtu              = IBV_MTU_1024;
	qp_attr.dest_qp_num           = remote_conn.qpn;
	qp_attr.rq_psn                = remote_conn.psn;
	qp_attr.max_dest_rd_atomic    = 1;
	qp_attr.min_rnr_timer         = 0x12;
	qp_attr.ah_attr.is_global     = 0;
	qp_attr.ah_attr.dlid          = remote_conn.lid;
	qp_attr.ah_attr.sl            = 0;
	qp_attr.ah_attr.src_path_bits = 0;
	qp_attr.ah_attr.port_num      = IB_PORT;
	// ethernet link set gid parameters.
	if(parameters.Ethlink){
		qp_attr.ah_attr.is_global = 1;
		memcpy(&qp_attr.ah_attr.grh.dgid, &remote_conn.gid, 16);
		qp_attr.ah_attr.port_num = IB_PORT;
//		qp_attr.ah_attr.grh.flow_label = 0;
		qp_attr.ah_attr.grh.sgid_index = 0;
		qp_attr.ah_attr.grh.hop_limit = 0xff;
		qp_attr.ah_attr.grh.traffic_class = 0;
	}

	if (ibv_modify_qp(qp, &qp_attr,
	                       IBV_QP_STATE              |
	                       IBV_QP_AV                 |
	                       IBV_QP_PATH_MTU           |
	                       IBV_QP_DEST_QPN           |
	                       IBV_QP_RQ_PSN             |
	                       IBV_QP_MAX_DEST_RD_ATOMIC |
						   IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "failed to modify qp state to RTR!\n");
		goto EXIT_DESTROY_EQP;
	}
	qp_attr.dest_qp_num           = remote_conn.qpn+1;

	if (ibv_modify_qp(eqp, &qp_attr,
	                       IBV_QP_STATE              |
	                       IBV_QP_AV                 |
	                       IBV_QP_PATH_MTU           |
	                       IBV_QP_DEST_QPN           |
	                       IBV_QP_RQ_PSN             |
	                       IBV_QP_MAX_DEST_RD_ATOMIC |
						   IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "failed to modify eqp state to RTR!\n");
		goto EXIT_DESTROY_EQP;
	}


	// set QP status to RTS; ready to perform data transfers
	qp_attr.qp_state      = IBV_QPS_RTS;
	qp_attr.timeout       = 14;
	qp_attr.retry_cnt     = 7;
	qp_attr.rnr_retry     = 7;
	qp_attr.sq_psn        = local_conn.psn;
	qp_attr.max_rd_atomic = 1;
	if (ibv_modify_qp(qp, &qp_attr,
	                       IBV_QP_STATE     |
	                       IBV_QP_TIMEOUT   |
	                       IBV_QP_RETRY_CNT |
	                       IBV_QP_RNR_RETRY |
	                       IBV_QP_SQ_PSN    |
	                       IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "failed to modify qp state to RTS!\n");
		goto EXIT_DESTROY_EQP;
	}
	if (ibv_modify_qp(eqp, &qp_attr,
	                       IBV_QP_STATE     |
	                       IBV_QP_TIMEOUT   |
	                       IBV_QP_RETRY_CNT |
	                       IBV_QP_RNR_RETRY |
	                       IBV_QP_SQ_PSN    |
	                       IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "failed to modify eqp state to RTS!\n");
		goto EXIT_DESTROY_EQP;
	}

	if(myrank && dbg){
		unsigned char c;
		int i,j;
		c = 0x01;
		memset(buf_cp, 0x00, buf_size);
		unsigned char *buf = (unsigned char *)buf_cp;
		for (i = 0; i < parameters.block_num; i++) 
			memset(buf + i * parameters.stride, c++, parameters.block_size);
		printf("================original buf data===============\n");
			for (j = 0; j < buf_size; j++){
				printf("%x ", buf[j]);
			}
		printf("\n================================================\n");
	}
	// bench S/G performance of send/recv
//	for (int test = SGRS; test < TEST_END; test++) {
	int test; test = SGRS;{
		struct ibv_send_wr *bad_wr;
		struct ibv_recv_wr *bad_rr;
		struct ibv_wc wc;
		struct ibv_wc nwc[sglist_num+1];
		struct ibv_exp_wc ewc;
		struct ibv_exp_send_wr *bad_esr;//for umr
		struct ibv_exp_mem_region  mem_reg_list[mr_num];
		int umr_length;
		
 		struct ibv_sge list;

		unsigned char *buf;

		uint64_t tick;
		uint64_t ticks[parameters.iterN];

		int i, j, m, n;
		int ne, ne_sum;
		unsigned char c;
		umr_length = 0;
		ne_sum = 0;
		// prepare the buffers
		memset(buf_sg, 0x00, buf_size); 
		memset(buf_cp, 0x00, buf_size);
		memset(buf_umr, 0x00, buf_size);
		c = 0x01;
		if (0 == myrank) {
			buf = (test == SR_COPY) ? (unsigned char *)buf_cp : (unsigned char *)buf_umr;
			for (i = 0; i < parameters.block_num; i++) {
				memset(buf + i * parameters.stride, c++, parameters.block_size);
			}
		}

		// prepare the S/G entries for SR_COPY
		memset(sg_list, 0, sizeof(struct ibv_sge) * parameters.block_num);
		for (i = 0; i < parameters.block_num; i++) {
			sg_list[i].addr   = ((uintptr_t)buf_sg) + i * parameters.stride;
			sg_list[i].length = (test == SGRS) ? parameters.block_size : parameters.block_size * parameters.block_num;
			sg_list[i].lkey   = mr->lkey;

			if (test == SR_COPY) break;
		}

		// prepare the S/G entries for SGRS about buf_umr
		memset(esg_list, 0, sizeof(struct ibv_sge) * parameters.block_num);
		for (i = 0; i < parameters.block_num; i++) {
			esg_list[i].addr   = ((uintptr_t)buf_umr) + i * parameters.stride;
			esg_list[i].length = parameters.block_size;
			esg_list[i].lkey   = emr[i]->lkey;
		}

		// prepare memory region list
		memset(&mem_reg_list[0], 0, sizeof(ibv_exp_mem_region)*mr_num);	
		for(i=0; i<mr_num; i++){
			mem_reg_list[i].base_addr = (uint64_t)(uintptr_t)emr[i]->addr;
			mem_reg_list[i].mr = emr[i];
			mem_reg_list[i].length = emr[i]->length;
			umr_length += mem_reg_list[i].length;
		}
		// create UMR
		esr = (const struct ibv_exp_send_wr){ 0 };
		esr.exp_opcode     = IBV_EXP_WR_UMR_FILL;
		esr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
		esr.ext_op.umr.umr_type = IBV_EXP_UMR_MR_LIST;
		esr.ext_op.umr.memory_objects = mem_objects;
		esr.ext_op.umr.exp_access = IBV_EXP_ACCESS_LOCAL_WRITE | IBV_EXP_ACCESS_REMOTE_WRITE;
		esr.ext_op.umr.modified_mr = modify_mr;
		esr.ext_op.umr.base_addr = (uint64_t)(uintptr_t)emr[0]->addr;
		esr.ext_op.umr.num_mrs = mr_num;
		esr.ext_op.umr.mem_list.mem_reg_list = mem_reg_list;

		//post WR to HCA to accomplish creating UMR
		ne = ibv_exp_post_send(eqp, &esr, &bad_esr);
		if(ne){
			fprintf(stderr, "myrank:%d, return:%x, failed to post EXP_WR!\n",myrank, ne);
			goto EXIT_DESTROY_EQP;
		}
		do ne = ibv_exp_poll_cq(ecq, 1, &ewc, sizeof(ewc)); while (ne == 0);
		if (ne < 0) {
			fprintf(stderr, "rank%d failed to read ECQ!\n", myrank);
			goto EXIT_DESTROY_EQP;
		}
		if (ewc.status != IBV_WC_SUCCESS) {
			fprintf(stderr, "rank%d failed to CREATE UMR\n", myrank);
			goto EXIT_DESTROY_EQP;
		}
		modify_mr->length = umr_length;
		modify_mr->addr =  (void *)(unsigned long)esr.ext_op.umr.base_addr;
	
		// sg list for UMR post send/recv_wr
		list.addr = (uintptr_t)modify_mr->addr;
		list.length = modify_mr->length;
		list.lkey = modify_mr->lkey;
		//print for debug
		if(test == UMR && dbg)
		printf("myrank:%d, buf_umr at 0x%x, sge.addr is 0x%x, sge.length is %d, UMR->length is %d, UMR->pd is0x%x, UMR->addr is 0x%x\n", myrank, (uintptr_t)buf_umr, list.addr, list.length, modify_mr->length, modify_mr->pd, modify_mr->addr);
		if (myrank) {
			// receiver prepares the receive request
			if(test == SR_COPY){
				rr = (const struct ibv_recv_wr){ 0 };
				rr.wr_id   = WR_ID;
				rr.sg_list = sg_list;
				rr.num_sge = 1;
			} else {
				//TODO:  need use UMR
				rr = (const struct ibv_recv_wr){ 0 };
				rr.wr_id   = WR_ID;
				rr.sg_list = &list;
				rr.num_sge = 1;
			}
		} else {
			// sender prepares the send request
			if(test == SR_COPY ){
				sr = (const struct ibv_send_wr){ 0 };
				sr.wr_id      = WR_ID;
				sr.sg_list    = sg_list;
				sr.num_sge    = 1;
				sr.opcode     = IBV_WR_SEND;
				sr.send_flags = IBV_SEND_SIGNALED;
			} else if(test == SGRS) {
				for (i=0; i<sglist_num; i++ ){
					nsr[i] = (const struct ibv_send_wr){ 0 };
					nsr[i].wr_id = WR_ID+i+1;
					nsr[i].next = (i == (sglist_num-1)) ? NULL : &nsr[i+1];
					nsr[i].sg_list = &esg_list[dev_attr.max_sge*i];
					nsr[i].num_sge = (sglist_num > 1) ? ((i == (sglist_num-1)) ? last_sge_num : dev_attr.max_sge) : parameters.block_num; 
					nsr[i].opcode = IBV_WR_RDMA_WRITE;
					nsr[i].send_flags = IBV_SEND_SIGNALED;
					nsr[i].wr.rdma.remote_addr = remote_conn.addr+i*parameters.block_size*dev_attr.max_sge;
					nsr[i].wr.rdma.rkey = remote_conn.rkey;

				}
			} else{
				//TODO:  need use UMR
				sr = (const struct ibv_send_wr){ 0 };
				sr.wr_id      = WR_ID;
				sr.sg_list    = &list;
				sr.num_sge    = 1;
				sr.opcode     = (test == UMR) ? IBV_WR_SEND : IBV_WR_RDMA_WRITE;
				sr.send_flags = IBV_SEND_SIGNALED;
				if(test == UMR_W){
					sr.wr.rdma.remote_addr = remote_conn.addr;
					sr.wr.rdma.rkey = remote_conn.rkey;
				}
			}
			// then prepares timing
			min_tick = 0xffffffffffffffffUL;
			max_tick = 0;
			memset(ticks, 0, sizeof(uint64_t) * parameters.iterN);
		}

		// start iteration
		for (i = 0; i < (parameters.iterN + parameters.iterW); i++) {
			if (myrank) {
				// post receive WR
				if((test == SR_COPY)){
					if (ibv_post_recv(qp, &rr, &bad_rr)) {
						fprintf(stderr, "failed to post receive WR!\n");
						goto EXIT_DESTROY_EQP;
					}
				}else if(test == UMR){
					if (ibv_post_recv(eqp, &rr, &bad_rr)) {
						fprintf(stderr, "failed to post UMR receive WR!\n");
						goto EXIT_DESTROY_EQP;
					}
				}	
				// wait for send
				MPI_Barrier(MPI_COMM_WORLD);
			} else if (!myrank){
				// wait for recv
				MPI_Barrier(MPI_COMM_WORLD);

				// start timing at the sender side
				tick = rdtsc();

				// copy the buffer
				if (test == SR_COPY) {
					for (j = 0; j < parameters.block_num; j++) {
						memcpy((unsigned char *)buf_sg + j * parameters.block_size, (unsigned char *)buf_cp + j * parameters.stride, parameters.block_size);
					}
				}
				// post send WR
				if(test == SR_COPY){
					if (ibv_post_send(qp, &sr, &bad_wr)) {
						fprintf(stderr, "failed to post WR!\n");
						goto EXIT_DESTROY_EQP;
					}	
				} else if(test == SGRS) {
						if (ibv_post_send(qp, &nsr[0], &bad_wr)) {
							fprintf(stderr, "SGRS failed to post %dst send WR!\n",i);
							goto EXIT_DESTROY_EQP;
						}
						
				}else{
					if (ibv_post_send(eqp, &sr, &bad_wr)) {
						fprintf(stderr, "failed to post UMR WR!\n");
						goto EXIT_DESTROY_EQP;
					}
				}
			}

			// poll the CQ
			if(test == SR_COPY){
				do ne = ibv_poll_cq(cq, 1, &wc); while (ne == 0);
				if (ne < 0) {
					fprintf(stderr, "rank%d failed to read CQ!\n", myrank);
					goto EXIT_DESTROY_EQP;
				}
				if (wc.status != IBV_WC_SUCCESS) {
					fprintf(stderr, "rank%d failed to execute WR!\n", myrank);
					fprintf(stderr, "rank%d Work completion status is:%s", myrank, ibv_wc_status_str(wc.status));
					goto EXIT_DESTROY_EQP;
				}
			} else if(test == SGRS) {
				if(!myrank){
					do {
						ne = ibv_poll_cq(cq, sglist_num-ne_sum, &nwc[ne_sum]);
						if (ne < 0) {
							fprintf(stderr, "rank%d failed to read CQ!\n", myrank);
							goto EXIT_DESTROY_EQP;
						}else ne_sum = ne + ne_sum;
						if(dbg)	printf("iter%d rank%d ne is %d ne_sum is %d\n",i, myrank, ne, ne_sum);
				
					} while (ne_sum < sglist_num);
					//} while (ne < sglist_num);
					for(j=0; j<sglist_num; j++){
						if(dbg) fprintf(stderr, "iter%d rank%d Work completion status is:%s ID is %d.\n",i, myrank, ibv_wc_status_str(nwc[j].status), nwc[j].wr_id);
						if (nwc[j].status != IBV_WC_SUCCESS) {
							fprintf(stderr, "rank%d failed to execute WR!\n", myrank);
							fprintf(stderr, "rank%d Work completion status is:%s ID is %d.\n", myrank, ibv_wc_status_str(nwc[j].status), nwc[j].wr_id);
							goto EXIT_DESTROY_EQP;
						}	
					}
					ne_sum = 0;
				}
			}else if(test == UMR){
				do ne = ibv_exp_poll_cq(ecq, 1, &ewc, sizeof(ewc)); while (ne ==0);
				if (ne < 0) {
					fprintf(stderr, "rank%d failed to read UMR CQ!\n", myrank);
					goto EXIT_DESTROY_EQP;
				}
				if (ewc.status != IBV_WC_SUCCESS) {
					fprintf(stderr, "rank%d failed to execute UMR WR!\n", myrank);
					fprintf(stderr, "rank%d UMR Work completion status is:%s", myrank, ibv_wc_status_str(ewc.status));
					goto EXIT_DESTROY_EQP;
				}
			}else if((test == UMR_W) && (!myrank)){
				do ne = ibv_exp_poll_cq(ecq, 1, &ewc, sizeof(ewc)); while (ne ==0);
				if (ne < 0) {
					fprintf(stderr, "rank%d failed to read UMR CQ!\n", myrank);
					goto EXIT_DESTROY_EQP;
				}
				if (ewc.status != IBV_WC_SUCCESS) {
					fprintf(stderr, "rank%d failed to execute UMR WR!\n", myrank);
					fprintf(stderr, "rank%d UMR Work completion status is:%s.", myrank, ibv_wc_status_str(ewc.status));
					goto EXIT_DESTROY_EQP;
				}
			}
			if (myrank) {
				//if(dbg){
				// receiver verifies the buffer
				MPI_Barrier(MPI_COMM_WORLD);
				buf = (test == SR_COPY) ? (unsigned char *)buf_sg : (unsigned char *)buf_umr;
				c = 0x01;
				if(dbg){
				printf("[%s] ", (test == SGRS) ? "send sgrs+recv umr_write" : ((test == UMR) ? "umr_sr" : ((test == UMR_W) ? "umr_write " : "sr_copy")));
				printf("================received buf data===============\n");
				for (j = 0; j < buf_size; j++){
					printf("%x ", buf[j]);
				}
				printf("\n================================================\n");}
				for (m = 0; m < parameters.block_num; m++) {
					for (n = 0; n < parameters.block_size; n++) {
						j = (test != SR_COPY) ? (m * parameters.stride + n) : (m * parameters.block_size + n);
						if (buf[j] != c) {
							fprintf(stderr, "failed to verify the received data @%d!\n", j);
							break;
						}
					}
					c++;
				}
				//}
			}else if(!myrank){
				// copy the buffer
				if (test == SR_COPY) {
					for (j = 0; j < parameters.block_num; j++) {
						memcpy((unsigned char *)buf_cp + j * parameters.stride, (unsigned char *)buf_sg + j * parameters.block_size, parameters.block_size);
					}
				}

				// finish timing at sender side
				tick = rdtsc() - tick;
				if (i >= parameters.iterW) {
					ticks[i-parameters.iterW] = tick;
					if (tick < min_tick) min_tick = tick;
					if (tick > max_tick) max_tick = tick;
				}
				MPI_Barrier(MPI_COMM_WORLD);
			}
		}// end of iteration

		// print timing result
		if (0 == myrank) {
			if(test == 0)
				printf("\n==================latency results================\n");
			printf("[%s] ", (test == SGRS) ? "send sgrs+recv umr_write" : ((test == UMR) ? "umr_sr" :( (test == UMR_W) ? "umr_write " : "sr_copy")));
			print_timing(parameters.iterN, ticks);
		}
	} // end of situation
EXIT_DESTROY_EQP:
	ibv_destroy_qp(eqp);

EXIT_DEALLOC_MKEY_LIST:
	ibv_exp_dealloc_mkey_list_memory(mem_objects);

EXIT_DESTROY_MODIFYMR:
	ibv_dereg_mr(modify_mr);

EXIT_DESTROY_QP:
	ibv_destroy_qp(qp);

EXIT_DESTROY_ECQ:
	ibv_destroy_cq(ecq);

EXIT_DESTROY_CQ:
	ibv_destroy_cq(cq);

EXIT_DEREG_EMR:
	for(imr=0; imr<mr_num; imr++)
	ibv_dereg_mr(emr[imr]);

EXIT_DEREG_MR:
	ibv_dereg_mr(mr);

EXIT_FREE_BUF:
	if (buf_sg) free(buf_sg);
	if (buf_cp) free(buf_cp);
	if (buf_umr) free(buf_umr);

EXIT_DEALLOC_PD:
	ibv_dealloc_pd(pd);

EXIT_CLOSE_DEV:
	ibv_close_device(dev_ctx);

EXIT_FREE_DEV_LIST:
	ibv_free_device_list(dev_list);

EXIT_MPI_FINALIZE:
	MPI_Finalize();

	return 0;
}

