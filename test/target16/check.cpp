//******************************************************************************
//*
//*     scmRTOS 16-bit target check
//*
//*     Each function below calls real kernel code with constant arguments.
//*     Compiled by clang at -O2 for a 16-bit-int target, each folds to a
//*     constant: `ret i32 <value>` in the LLVM IR. run.sh compares the values
//*     with the expected ones: "EXPECT name value [value-for-descending-order]".
//*
//*     With 16 user processes TProcessMap is 32 bits wide, `unsigned` 16 bits.
//*
//******************************************************************************
#include <scmRTOS.h>

using namespace OS;

struct TProbe : public TService
{
    static TProcessMap highest_tag(TProcessMap m) { return highest_prio_tag(m); }
};

extern "C"
{
    // sizes, to make sure the target is what the check assumes
    // EXPECT sizeof_unsigned 2
    uint32_t sizeof_unsigned()   { return sizeof(unsigned); }
    // EXPECT sizeof_process_map 4
    uint32_t sizeof_process_map() { return sizeof(TProcessMap); }

    // clr_prio_tag(): removing pr3 from "all 17 ready" must keep bit 16 (idle)
    // EXPECT clr_low_tag_keeps_high_bits 0x1FFF7
    uint32_t clr_low_tag_keeps_high_bits() { TProcessMap m = 0x1FFFFul; clr_prio_tag(m, get_prio_tag(3)); return m; }

    // clr_prio_tag(): removing a tag above bit 15 must remove just that bit
    // EXPECT clr_high_tag 0xFFFF
    uint32_t clr_high_tag() { TProcessMap m = 0x1FFFFul; clr_prio_tag(m, get_prio_tag(16)); return m; }

    // highest_prio_tag(): the only waiter is above bit 15
    // EXPECT highest_tag_above_bit15 0x10000
    uint32_t highest_tag_above_bit15() { return TProbe::highest_tag(0x10000ul); }

    // highest_prio_tag(): a low and a high waiter; the highest priority is the
    // lowest bit with ascending priority order, the highest bit with descending
    // EXPECT highest_tag_mixed 0x8 0x10000
    uint32_t highest_tag_mixed() { return TProbe::highest_tag(0x10008ul); }

    // highest_priority() of the port for a map with only bit 16 set
    // EXPECT highest_priority_bit16 16
    uint32_t highest_priority_bit16() { return highest_priority(0x10000ul); }
}
