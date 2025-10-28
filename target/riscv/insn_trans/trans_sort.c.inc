static bool trans_sort(DisasContext *ctx, arg_sort *a)
{
    TCGv src_addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv size = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv involve_size = get_gpr(ctx, a->rd, EXT_NONE);

    gen_helper_sort(tcg_env, src_addr, size, involve_size);
    return true;
}