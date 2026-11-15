/*
        The card's address space as the model has it: the job's regions (gpu_model.region), each an address range of the card's, the
        processor's memory behind it, and what the card may do there (GPU_MODEL_READ, WRITE, RUN). An access is one region's or none: a load,
        a store or a fetch with any byte outside every region that allows it is refused with the address, its bytes, the lane and the place in
        the code (the bounds checker for hostile records: the card would read or write some other buffer, or fault).
*/

// The memory for va to va + bytes, if one region holds all of it and allows how: else null.
static p8 address_to gpu_model_reach(positive va, positive bytes, unsigned int how)
{
        unsigned int at = gpu_model.last < gpu_model.regions ? gpu_model.last : 0;

        for (unsigned int k = 0; k < gpu_model.regions; k++, at = at + 1 < gpu_model.regions ? at + 1 : 0)
        {
                const struct gpu_model_region *r = gpu_model.region + at;

                if (va >= r->va && va - r->va < r->bytes && bytes <= r->bytes - (va - r->va) && (r->can & how) == how && r->host)
                {
                        gpu_model.last = at;
                        return r->host + (va - r->va);
                }
        }
        return null;
}

// The refusal of an access no region allows.
static int gpu_model_bounds(positive va, positive bytes, unsigned int how, unsigned int lane)
{
        if (gpu_model_fail(GPU_MODEL_BOUNDS, how & GPU_MODEL_WRITE ? "a store of " : how & GPU_MODEL_RUN ? "a fetch of " : "a load of "))
                return gpu_model.fault.code;
        gpu_model.fault.address = va, gpu_model.fault.bytes = (unsigned int)bytes, gpu_model.fault.lane = lane;
        gpu_model_number(bytes, 10);
        gpu_model_put(" bytes at ");
        gpu_model_number(va, 16);
        gpu_model_put(how & GPU_MODEL_WRITE && gpu_model_reach(va, bytes, GPU_MODEL_READ) ? " that the job may read and not write" : " outside every buffer the job named");
        gpu_model_put(", lane ");
        gpu_model_number(lane, 10);
        return gpu_model_fail_end();
}

// bytes (1 to 32) at va into into, read as the card reads them: 0, or the refusal.
static int gpu_model_load(positive va, unsigned int bytes, p8 address_to into, unsigned int lane)
{
        const p8 *from = gpu_model_reach(va, bytes, GPU_MODEL_READ);

        if (!from)
                return gpu_model_bounds(va, bytes, GPU_MODEL_READ, lane);
        memory_copy(into, from, bytes);
        return 0;
}

static int gpu_model_store(positive va, unsigned int bytes, const p8 address_to from, unsigned int lane)
{
        p8 address_to into = gpu_model_reach(va, bytes, GPU_MODEL_READ | GPU_MODEL_WRITE);

        if (!into)
                return gpu_model_bounds(va, bytes, GPU_MODEL_WRITE, lane);
        memory_copy(into, from, bytes);
        return 0;
}
