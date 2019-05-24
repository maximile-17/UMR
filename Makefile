TESTS       := sgrs_umr sgrs_umr_asym sgrs_umr_asym_try sgrs_umr_length sgrs_umr_largelist sgrs_umr_nmr sgrs_stencil_w sgrs_stencil_sr umr_stencil nas_mg_z_sr nas_mg_z_w sgrs_umr_cache ssgrs_rumr_largelist

MPI_PATH    = /home/mxx/opt/openmpi-4.0.0

CXX         := g++
CXXFLAGS    := -O2
INCLUDES    := -I. -I$(MPI_PATH)/include
LINK        := g++
LIBRARIES   := -libverbs -L. -L$(MPI_PATH)/lib -lmpi
LDFLAGS     := -fPIC -Wl,-rpath -Wl,\$$ORIGIN -Wl,-rpath -Wl,$(MPI_PATH)/lib


.PHONY: all
all: build
	@echo -e "\033[1;32mCONSTRUCTION COMPLETE!\033[0m"

.PHONY: build
build: $(TESTS)

%.o: %.c
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<

$(TESTS): %: %.o
	$(LINK) $(LDFLAGS) -o $@ $+ $(LIBRARIES)


.PHONY: clean
clean: 
	rm -f *.o
	rm -f $(TESTS)
