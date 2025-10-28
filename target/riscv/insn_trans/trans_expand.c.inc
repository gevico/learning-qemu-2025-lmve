static bool trans_expand(DisasContext *ctx, arg_expand *a)
{
    TCGv dst = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv num = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_expand(tcg_env, dst, src, num);
    return true;
}