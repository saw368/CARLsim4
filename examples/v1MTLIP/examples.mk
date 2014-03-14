# module include file for simple
example := v1MTLIP
output := *.dot *.txt *.log 

# local info (vars can be overwritten)
local_dir := examples/$(example)
local_src := $(local_dir)/main_$(example).cpp
local_objs := $(local_dir)/v1ColorME.2.1.o
local_prog := $(local_dir)/$(example)

# info passed up to Makfile
sources += $(local_src)
carlsim_programs += $(local_prog)
output_files += $(addprefix $(local_dir)/,$(output))
all_targets += $(local_prog)
objects += $(local_objs)

v1MTLIP_objs := $(local_objs)

.PHONY: $(example)
$(example): $(local_src) $(local_prog)

$(local_prog): $(local_src) $ $(carlsim_deps) $(carlsim_objs) $(local_objs)
	$(NVCC) $(CARLSIM_INCLUDES) $(CARLSIM_LFLAGS) $(CARLSIM_LIBS) \
	$(CARLSIM_FLAGS) $(carlsim_objs) $(v1MTLIP_objs) $< -o $@

# local cuda
$(local_dir)/%.o: $(local_dir)/%.cu $(local_deps)
	$(NVCC) -c $(CARLSIM_INCLUDES) $(CARLSIM_LFLAGS) $(CARLSIM_LIBS) \
	$(CARLSIM_FLAGS) $< -o $@