static bool trans_crush(DisasContext *ctx, arg_crush *a)
{
    TCGv dst = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv num = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_crush(tcg_env, dst, src, num);
    return true;
}