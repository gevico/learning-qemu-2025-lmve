
static bool trans_dma(DisasContext *ctx, arg_dma *a)
{
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst_addr = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv grain_size = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_dma(tcg_env, dst_addr, src_addr, grain_size);

    return true;
}