#include <xbyak.h>

/* x64 regisuter usage
 *  http://msdn.microsoft.com/en-US/library/9z1stfyw(v=vs.80).aspx
 * RAX          Volatile Return       value register
 * RCX          Volatile              First integer argument
 * RDX          Volatile              Second integer argument
 * R8           Volatile              Third integer argument
 * R9           Volatile              Fourth integer argument
 * R10:R11      Volatile              Must be preserved as needed by caller; used in syscall/sysret instructions
 * R12:R15      Nonvolatile           Must be preserved by callee
 * RDI          Nonvolatile           Must be preserved by callee
 * RSI          Nonvolatile           Must be preserved by callee
 * RBX          Nonvolatile           Must be preserved by callee
 * RBP          Nonvolatile           May be used as a frame pointer; must be preserved by callee
 * RSP          Nonvolatile           Stack pointer
 * XMM0         Volatile              First FP argument
 * XMM1         Volatile              Second FP argument
 * XMM2         Volatile              Third FP argument
 * XMM3         Volatile              Fourth FP argument
 * XMM4:XMM5    Volatile              Must be preserved as needed by caller
 * XMM6:XMM15   Nonvolatile           Must be preserved as needed by callee.
 */

char __declspec(align(32)) zero_mem[4096*8];

enum lt_op {
    LT_LATENCY,
    LT_THROUGHPUT,
    LT_THROUGHPUT_KILLDEP
};

enum operand_type {
    OT_INT,
    OT_FP32,
    OT_FP64
};

template <typename T> struct RegMap;

template <>
struct RegMap<Xbyak::Xmm>
{
    const char *name;
    Xbyak::Xmm v8, v9, v10, v11, v12, v13, v14, v15;

    RegMap()
        :name("m128"), v8(8), v9(9), v10(10), v11(11), v12(12), v13(13), v14(14), v15(15)
        {}

    void save(Xbyak::CodeGenerator *g, Xbyak::Xmm r, int off, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->movdqa(g->ptr [g->rsp + off], r);
            break;
        case OT_FP32:
            g->movaps(g->ptr [g->rsp + off], r);
            break;
        case OT_FP64:
            g->movapd(g->ptr [g->rsp + off], r);
            break;
        }
    }

    void restore(Xbyak::CodeGenerator *g, Xbyak::Xmm r, int off, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->movdqa(r, g->ptr [g->rsp + off]);
            break;
        case OT_FP32:
            g->movaps(r, g->ptr [g->rsp + off]);
            break;
        case OT_FP64:
            g->movapd(r, g->ptr [g->rsp + off]);
            break;
        }
    }

    void killdep(Xbyak::CodeGenerator *g, Xbyak::Xmm r, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->pxor(r, r);
            break;
        case OT_FP32:
            g->xorps(r, r);
            break;
        case OT_FP64:
            g->xorpd(r, r);
            break;
        }
    }
};

template <>
struct RegMap<Xbyak::Ymm>
{
    const char *name;
    Xbyak::Ymm v8, v9, v10, v11, v12, v13, v14, v15;

    RegMap()
        :name("m256"), v8(8), v9(9), v10(10), v11(11), v12(12), v13(13), v14(14), v15(15)
        {}

    void save(Xbyak::CodeGenerator *g, Xbyak::Ymm r, int off, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->vmovdqa(g->ptr [g->rsp + off], r);
            break;
        case OT_FP32:
            g->vmovaps(g->ptr [g->rsp + off], r);
            break;
        case OT_FP64:
            g->vmovapd(g->ptr [g->rsp + off], r);
            break;
        }
    }

    void restore(Xbyak::CodeGenerator *g, Xbyak::Ymm r, int off, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->vmovdqa(r, g->ptr [g->rsp + off]);
            break;
        case OT_FP32:
            g->vmovaps(r, g->ptr [g->rsp + off]);
            break;
        case OT_FP64:
            g->vmovapd(r, g->ptr [g->rsp + off]);
            break;
        }
    }

    void killdep(Xbyak::CodeGenerator *g, Xbyak::Ymm r, enum operand_type ot) {
        switch (ot) {
        case OT_INT:
            g->vpxor(r, r, r);
            break;
        case OT_FP32:
            g->vxorps(r, r, r);
            break;
        case OT_FP64:
            g->vxorpd(r, r, r);
            break;
        }
    }
};



template <>
struct RegMap<Xbyak::Reg64>
{
    const char *name;
    Xbyak::Reg64 v8, v9, v10, v11, v12, v13, v14, v15;

    RegMap()
        :name("reg64"),
         v8(Xbyak::Operand::R8),
         v9(Xbyak::Operand::R9),
         v10(Xbyak::Operand::R10),
         v11(Xbyak::Operand::R11),
         v12(Xbyak::Operand::R12),
         v13(Xbyak::Operand::R13),
         v14(Xbyak::Operand::R14),
         v15(Xbyak::Operand::R15)
        {}

    void save(Xbyak::CodeGenerator *g, Xbyak::Reg64 r, int off, enum operand_type ) {
        g->mov(g->ptr[g->rsp + off], r);
    }

    void restore(Xbyak::CodeGenerator *g, Xbyak::Reg64 r, int off, enum operand_type ) {
        g->mov(r, g->ptr[g->rsp + off]);
    }

    void killdep(Xbyak::CodeGenerator *g, Xbyak::Reg64 r, enum operand_type) {
        g->xor(r, r);
    }

};

template <typename RegType,
          typename F>
struct Gen
    :public Xbyak::CodeGenerator
{
    Gen(F f, int num_loop, int num_insn, enum lt_op o, enum operand_type ot) {
        RegMap<RegType> rm;

        push(rbp);
        mov(rbp, rsp);
        and(rsp, -(__int64)32);
        sub(rsp, 32 * 9);

        rm.save(this, rm.v8,  -32*8, ot);
        rm.save(this, rm.v9,  -32*7, ot);
        rm.save(this, rm.v10, -32*6, ot);
        rm.save(this, rm.v11, -32*5, ot);
        rm.save(this, rm.v12, -32*4, ot);
        rm.save(this, rm.v13, -32*3, ot);
        rm.save(this, rm.v14, -32*2, ot);
        rm.save(this, rm.v15, -32*1, ot);

        rm.killdep(this, rm.v8, ot);
        rm.killdep(this, rm.v9, ot);
        rm.killdep(this, rm.v10, ot);
        rm.killdep(this, rm.v11, ot);
        rm.killdep(this, rm.v12, ot);
        rm.killdep(this, rm.v13, ot);
        rm.killdep(this, rm.v14, ot);
        rm.killdep(this, rm.v15, ot);

        mov(rcx, num_loop);
        mov(rdx, (intptr_t)zero_mem);
        mov(ptr[rsp], rdi);
        xor(rdi, rdi);

        L("@@");

        switch (o) {
        case LT_LATENCY:
            for (int ii=0; ii<num_insn; ii++) {
                f(this, rm.v8, rm.v8);
            }
            break;

        case LT_THROUGHPUT:
            for (int ii=0; ii<num_insn/8; ii++) {
                f(this, rm.v8, rm.v8);
                f(this, rm.v9, rm.v9);
                f(this, rm.v10, rm.v10);
                f(this, rm.v11, rm.v11);
                f(this, rm.v12, rm.v12);
                f(this, rm.v13, rm.v13);
                f(this, rm.v14, rm.v14);
                f(this, rm.v15, rm.v15);
            }
            break;

        case LT_THROUGHPUT_KILLDEP:
            for (int ii=0; ii<num_insn/8; ii++) {
                f(this, rm.v8, rm.v8);
                f(this, rm.v9, rm.v9);
                f(this, rm.v10, rm.v10);
                f(this, rm.v11, rm.v11);
                f(this, rm.v12, rm.v12);
                f(this, rm.v13, rm.v13);
                f(this, rm.v14, rm.v14);
                f(this, rm.v15, rm.v15);
            }

            rm.killdep(this, rm.v8, ot);
            rm.killdep(this, rm.v9, ot);
            rm.killdep(this, rm.v10, ot);
            rm.killdep(this, rm.v11, ot);
            rm.killdep(this, rm.v12, ot);
            rm.killdep(this, rm.v13, ot);
            rm.killdep(this, rm.v14, ot);
            rm.killdep(this, rm.v15, ot);
            break;
        }

        dec(rcx);
        jnz("@b");

        mov(rdi, ptr[rsp]);
        rm.restore(this, rm.v8,  -32*8, ot);
        rm.restore(this, rm.v9,  -32*7, ot);
        rm.restore(this, rm.v10, -32*6, ot);
        rm.restore(this, rm.v11, -32*5, ot);
        rm.restore(this, rm.v12, -32*4, ot);
        rm.restore(this, rm.v13, -32*3, ot);
        rm.restore(this, rm.v14, -32*2, ot);
        rm.restore(this, rm.v15, -32*1, ot);

        mov(rsp, rbp);
        pop(rbp);
        ret();
            
        /*
         * latency:
         *
         *       mov rcx, count
         * loop:
         *       op reg, reg
         *       op reg, reg
         *       ...
         *       op reg, reg
         *       dec rcx
         *       jne loop
         *
         */

        /*
         * throughput
         *
         *       mov rcx, count
         * loop:
         *       op reg8, reg8
         *       op reg9, reg9
         *       ...
         *       op reg15, reg15
         *       op reg8, reg8
         *       op reg9, reg9
         *       ...
         *       op reg15, reg15
         *       ...
         *       if kill_dep {
         *       xor r8
         *       xor r9
         *       ...
         *       xor r15
         *       }
         *       dec rcx
         *       jne loop
         *
         */
        
    }
};
    

template <typename RegType, typename F>
void
lt(const char *name,
   const char *on,
   F f,
   int num_loop,
   int num_insn,
   enum lt_op o,
   enum operand_type ot)
{
    Gen<RegType,F> g(f, num_loop, num_insn, o, ot);
    typedef void (*func_t)(void);
    func_t exec = (func_t)g.getCode();

    exec();

    __int64 b = __rdtsc();
    exec();
    __int64 e = __rdtsc();

    printf("%8s:%10s:%10s: CPI=%8.2f, IPC=%8.2f\n",
           RegMap<RegType>().name,
           name, on,
           (e-b)/(double)(num_insn * num_loop), 
           (num_insn * num_loop)/(double)(e-b));
}           

#define NUM_LOOP (16384*8)

template <typename RegType, typename F>
void
run(const char *name, F f, bool kill_dep, enum operand_type ot)
{
    lt<RegType>(name, "latency", f, NUM_LOOP, 16, LT_LATENCY, ot);
    if (kill_dep) {
        lt<RegType>(name, "throughput", f, NUM_LOOP, 16, LT_THROUGHPUT_KILLDEP, ot);
    } else {
        lt<RegType>(name, "throughput", f, NUM_LOOP, 16, LT_THROUGHPUT, ot);
    }
}

template <typename RegType, typename F_t, typename F_l>
void
run_latency(const char *name, F_t f_t, F_l f_l, bool kill_dep, enum operand_type ot)
{
    lt<RegType>(name, "latency", f_l, NUM_LOOP, 16, LT_LATENCY, ot);
    if (kill_dep) {
        lt<RegType>(name, "throughput", f_t, NUM_LOOP, 16, LT_THROUGHPUT_KILLDEP, ot);
    } else {
        lt<RegType>(name, "throughput", f_t, NUM_LOOP, 16, LT_THROUGHPUT, ot);
    }
}

#define GEN(rt, name, expr, kd, ot)                                     \
    run<Xbyak::rt>(                                                     \
    name,                                                               \
    [](Xbyak::CodeGenerator *g, Xbyak::rt dst, Xbyak::rt src){expr;},   \
    kd, ot);


#define GEN_latency(rt, name, expr_t, expr_l, kd, ot)                   \
    run_latency<Xbyak::rt>(                                             \
    name,                                                               \
    [](Xbyak::CodeGenerator *g, Xbyak::rt dst, Xbyak::rt src){expr_t;}, \
    [](Xbyak::CodeGenerator *g, Xbyak::rt dst, Xbyak::rt src){expr_l;}, \
    kd, ot);
        

int
main(int argc, char **argv)
{
    printf("== latency/throughput ==\n");
    GEN(Reg64, "add", (g->add(dst, src)), false, OT_INT);
    GEN(Reg64, "load", (g->mov(dst, g->ptr[src + g->rdx])), false, OT_INT);


    GEN(Xmm, "pxor", (g->pxor(dst, src)), false, OT_INT);
    GEN(Xmm, "padd", (g->paddd(dst, src)), false, OT_INT);
    GEN(Xmm, "pmuldq", (g->pmuldq(dst, src)), false, OT_INT);

    /* 128 */
    GEN_latency(Xmm, "loadps",
                (g->movaps(dst, g->ptr[g->rdx])),
                (g->movaps(dst, g->ptr[g->rdx + g->rdi])); (g->movq(g->rdi, dst)); ,
                false, OT_INT);

    GEN(Xmm, "xorps", (g->xorps(dst, src)), false, OT_FP32);
    GEN(Xmm, "addps", (g->addps(dst, src)), false, OT_FP32);
    GEN(Xmm, "mulps", (g->mulps(dst, src)), false, OT_FP32);
    GEN(Xmm, "blendps", (g->blendps(dst, src, 0)), false, OT_FP32);
    GEN(Xmm, "pshufb", (g->pshufb(dst, src)), false, OT_INT);
    GEN(Xmm, "pmullw", (g->pmullw(dst, src)), false, OT_INT);
    GEN(Xmm, "phaddd", (g->phaddd(dst, src)), false, OT_INT);

    GEN(Xmm, "pinsrd", (g->pinsrb(dst, g->edx, 0)), false, OT_INT);
    GEN(Xmm, "dpps", (g->dpps(dst, src, 0xff)), false, OT_FP32);
    GEN(Xmm, "cvtps2dq", (g->cvtps2dq(dst, src)), false, OT_FP32);

    /* 256 */
    GEN_latency(Ymm, "loadps",
                (g->vmovaps(dst, g->ptr[g->rdx])),
                (g->vmovaps(dst, g->ptr[g->rdx + g->rdi])); (g->movq(g->rdi, dst)); ,
                false, OT_FP32);

    GEN(Ymm, "xorps", (g->vxorps(dst, dst, src)), false, OT_FP32);
    GEN(Ymm, "mulps", (g->vmulps(dst, dst, src)), false, OT_FP32);
    GEN(Ymm, "addps", (g->vaddps(dst, dst, src)), false, OT_FP32);
    GEN(Ymm, "divps", (g->vdivps(dst, dst, src)), false, OT_FP32);
    GEN(Ymm, "divpd", (g->vdivpd(dst, dst, src)), false, OT_FP64);
    GEN(Ymm, "rsqrtps", (g->vrsqrtps(dst, dst)), false, OT_FP32);
    GEN(Ymm, "rcpps", (g->vrcpps(dst, dst)), false, OT_FP32);
    GEN(Ymm, "sqrtps", (g->vsqrtps(dst, dst)), false, OT_FP32);
    GEN(Ymm, "vperm2f128", (g->vperm2f128(dst,dst,src,0)), false, OT_FP32);
}