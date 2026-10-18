/* shape_use.c — Universal Shaping Engine (el de Microsoft, como lo implementa HarfBuzz): el
 * armado de las escrituras del sur y sudeste de Asia que no tienen shaper propio (tibetano,
 * cingalés, balinés, javanés, sundanés, buginés, tai tham, chakma, brahmi, limbu, lepcha…) y de
 * algunas que se unen (mongol, n'ko, adlam):
 *
 *   1. normalización; categorías USE (tools/gen-unicode.mjs); sílabas con la gramática del
 *      USE (sin ver CGJ ni el ZWNJ que va antes de una marca);
 *   2. máscaras: rphf en el comienzo de cada sílaba; formas de unión por sílaba (o las del
 *      árabe en las escrituras que se unen);
 *   3. locl, ccmp, nukt, akhn; rphf (un reph sustituido pasa a ser R); pref (la sustituida
 *      pasa a VPre); rkrf, abvf, blwf, half, pstf, vatu, cjct;
 *   4. ◌ en las sílabas rotas; reordenamiento: el reph antes del primer glifo post-base y las
 *      vocales pre-base al comienzo (o después del último halant);
 *   5. isol/init/medi/fina; abvs, blws, haln, pres, psts… y GPOS (marcas sin ancho antes). */
#include "shape_syl.h"

#include <stdlib.h>
#include <string.h>

#include "unicode.h"

static const char *const cats[] = { USE_NAMES };

static SylGrammar grammar = {
    cats, (int)(sizeof cats / sizeof *cats),
    "h = H | HVM | IS | Sk;"
    "consonant_modifiers = CMAbv* CMBlw* ((h B | SUB) CMAbv* CMBlw*)*;"
    "medial_consonants = MPre? MAbv? MBlw? MPst?;"
    "dependent_vowels = VPre* VAbv* VBlw* VPst* | H;"
    "vowel_modifiers = HVM? VMPre* VMAbv* VMBlw* VMPst*;"
    "final_consonants = FAbv* FBlw* FPst*;"
    "final_modifiers = FMAbv* FMBlw* | FMPst?;"
    "complex_syllable_start = (R | CS)? (B | GB);"
    "complex_syllable_middle = consonant_modifiers medial_consonants dependent_vowels vowel_modifiers (Sk B)*;"
    "complex_syllable_tail = complex_syllable_middle final_consonants final_modifiers;"
    "number_joiner_terminated_cluster_tail = (HN N)* HN;"
    "numeral_cluster_tail = (HN N)+;"
    "symbol_cluster_tail = SMAbv+ SMBlw* | SMBlw+;"
    "virama_terminated_cluster_tail = consonant_modifiers (IS | RK);"
    "virama_terminated_cluster = complex_syllable_start virama_terminated_cluster_tail;"
    "sakot_terminated_cluster_tail = complex_syllable_middle Sk;"
    "sakot_terminated_cluster = complex_syllable_start sakot_terminated_cluster_tail;"
    "standard_cluster = complex_syllable_start complex_syllable_tail;"
    "tail = complex_syllable_tail | sakot_terminated_cluster_tail | symbol_cluster_tail | virama_terminated_cluster_tail;"
    "broken_cluster = R? (tail | number_joiner_terminated_cluster_tail | numeral_cluster_tail);"
    "number_joiner_terminated_cluster = N number_joiner_terminated_cluster_tail;"
    "numeral_cluster = N numeral_cluster_tail?;"
    "symbol_cluster = (O | GB | SB) tail?;"
    "hieroglyph_cluster = SB* G HR? HM? SE* (J SB* (G HR? HM? SE*)?)*;",
    (const char *const[]){
        "virama_terminated_cluster ZWNJ?", "sakot_terminated_cluster ZWNJ?", "standard_cluster ZWNJ?",
        "number_joiner_terminated_cluster ZWNJ?", "numeral_cluster ZWNJ?", "symbol_cluster ZWNJ?",
        "hieroglyph_cluster ZWNJ?", "FMPst", "broken_cluster ZWNJ?", "any" }, 10, NULL,
};
/* Tipos de sílaba (como use_syllable_type_t) y de qué alternativa sale cada uno. */
enum { T_VIRAMA, T_SAKOT, T_STANDARD, T_NUMBER_JOINER, T_NUMERAL, T_SYMBOL, T_HIEROGLYPH, T_BROKEN, T_NON };
static const uint8_t alt_type[10] = { T_VIRAMA, T_SAKOT, T_STANDARD, T_NUMBER_JOINER, T_NUMERAL, T_SYMBOL, T_HIEROGLYPH, T_NON, T_BROKEN, T_NON };

/* Escrituras que van por el USE (las de hb_ot_shaper_categorize con etiqueta propia en la fuente). */
static const char use_tags[][5] = {
    "tibt", "mong", "sinh", "buhd", "hano", "tglg", "tagb", "limb", "tale", "bugi", "khar", "sylo", "tfng", "bali", "nko ",
    "phag", "cham", "kali", "lepc", "rjng", "saur", "sund", "egyp", "java", "kthi", "mtei", "lana", "tavt", "batk", "brah",
    "mand", "cakm", "plrd", "shrd", "takr", "dupl", "gran", "khoj", "sind", "mahj", "mani", "modi", "hmng", "phlp", "sidd",
    "tirh", "ahom", "mult", "adlm", "bhks", "marc", "newa", "gonm", "soyo", "zanb", "dogr", "gong", "rohg", "maka", "medf",
    "sogo", "sogd", "elym", "nand", "hmnp", "wcho", "chrs", "diak", "kits", "yezi", "cpmn", "ougr", "tnsa", "toto", "vith",
    "kawi", "nagm",
};

int shape_use_script(const char *tag) {
    for (size_t i = 0; i < sizeof use_tags / sizeof *use_tags; i++) if (!memcmp(use_tags[i], tag, 4)) return 1;
    return 0;
}

/* Las que se unen como el árabe (has_arabic_joining de HarfBuzz). */
static int arabic_joining(const char *tag) {
    static const char j[][5] = { "mong", "nko ", "phag", "mand", "mani", "phlp", "adlm", "rohg", "sogd", "chrs", "ougr" };
    for (size_t i = 0; i < sizeof j / sizeof *j; i++) if (!memcmp(j[i], tag, 4)) return 1;
    return 0;
}

#define CAT(i) ((int)OTF_AUX_CAT(r->aux[i]))
static void set_cat(GlyphRun *r, int i, int c) { r->aux[i] = (r->aux[i] & ~0xffu) | (uint32_t)c; }

static int syl_end(const GlyphRun *r, int i) {
    int e = i + 1;
    while (e < r->len && OTF_AUX_SYL(r->aux[e]) == OTF_AUX_SYL(r->aux[i])) e++;
    return e;
}

static void clear_subst(GlyphRun *r) { for (int i = 0; i < r->len; i++) r->aux[i] &= ~(uint32_t)OTF_AUX_SUBST; }

static int is_halant(const GlyphRun *r, int i) {
    int c = CAT(i);
    return (c == USE_H || c == USE_HVM || c == USE_IS) && !(r->aux[i] & OTF_AUX_LIGATED);
}

static int post_base(int c) {
    switch (c) {
    case USE_FAbv: case USE_FBlw: case USE_FPst: case USE_FMAbv: case USE_FMBlw: case USE_FMPst: case USE_MAbv:
    case USE_MBlw: case USE_MPst: case USE_MPre: case USE_VAbv: case USE_VBlw: case USE_VPst: case USE_VPre:
    case USE_VMAbv: case USE_VMBlw: case USE_VMPst: case USE_VMPre:
        return 1;
    default:
        return 0;
    }
}

static void reorder(GlyphRun *r, int start, int end) {
    int t = SYL_TYPE(r->aux[start]);
    if (!(t == T_VIRAMA || t == T_SAKOT || t == T_STANDARD || t == T_SYMBOL || t == T_BROKEN)) return;
    /* el reph va antes del primer glifo post-base (o al final) */
    if (CAT(start) == USE_R && end - start > 1)
        for (int i = start + 1; i < end; i++) {
            int pb = post_base(CAT(i)) || is_halant(r, i);
            if (pb || i == end - 1) {
                if (pb) i--;
                syl_move(r, start, i);
                break;
            }
        }
    /* las vocales pre-base, al comienzo o después del último halant */
    for (int i = start, j = start; i < end; i++) {
        int c = CAT(i);
        if (is_halant(r, i)) j = i + 1;
        else if ((c == USE_VPre || c == USE_VMPre) && !(r->aux[i] & OTF_AUX_NOTFIRST) && j < i) syl_move(r, i, j);
    }
}

int shape_use(Shaper *s, Otf *o, const uint32_t *in, int n, const char *tag, int (*reserve)(Shaper *, int)) {
    GlyphRun *r = &s->run;
    r->len = 0;
    int m = syl_normalize(s, o, in, n, NULL, 0);
    if (m <= 0 || reserve(s, m * 2 + 8)) return 0;
    uint8_t *cat = malloc((size_t)m * 2 + 2);
    int *idx = malloc(sizeof *idx * (size_t)(m + 1));
    if (!cat || !idx) { free(cat); free(idx); return 0; }
    uint8_t *fc = cat + m + 1;
    for (int i = 0; i < m; i++) cat[i] = (uint8_t)uc_use_cat(s->cp[i]);
    /* la gramática no ve CGJ (ni ZWJ ni selectores) ni el ZWNJ antes de una marca */
    int nf = 0;
    for (int i = 0; i < m; i++) {
        if (cat[i] == USE_CGJ) continue;
        if (cat[i] == USE_ZWNJ) {
            int j = i + 1;
            while (j < m && cat[j] == USE_CGJ) j++;
            if (j < m && uc_mark(s->cp[j])) continue;
        }
        idx[nf] = i;
        fc[nf++] = cat[i];
    }
    idx[nf] = m;
    for (int i = 0; i < m; i++) {
        r->g[i] = otf_cmap(o, s->cp[i]);
        r->cl[i] = s->order[i];
        r->mask[i] = OTF_F_GLOBAL;
        r->lig[i] = 0;
        r->aux[i] = cat[i] | (uint32_t)T_NON << 28 | 1u << 16;
    }
    int syl = 1;
    for (int k = 0; k < nf;) {
        int alt, l = syl_next(&grammar, fc + k, nf - k, &alt);
        syl = syl % 255 + 1;
        for (int i = idx[k]; i < idx[k + l]; i++)
            r->aux[i] = cat[i] | (uint32_t)syl << 16 | (uint32_t)alt_type[alt] << 28;
        k += l;
    }
    r->len = m;
    free(idx);

    const OtfPlan *p = otf_plan_kind(o, tag, OTF_KIND_USE);
    s->plan = p;
    /* rphf: el primer glifo si es R; si no, hasta los tres primeros */
    for (int i = 0; i < r->len;) {
        int e = syl_end(r, i), lim = CAT(i) == USE_R ? 1 : (e - i < 3 ? e - i : 3);
        for (int j = i; j < i + lim; j++) r->mask[j] |= OTF_F_RPHF;
        i = e;
    }
    /* formas de unión */
    if (arabic_joining(tag)) shape_joining_masks(s->cp, m, r->mask);
    else {
        int last_start = 0, last_form = -1;     /* 0 isol, 1 init, 2 medi, 3 fina */
        static const uint32_t forms[4] = { OTF_F_ISOL, OTF_F_INIT, OTF_F_MEDI, OTF_F_FINA };
        const uint32_t all = OTF_F_ISOL | OTF_F_INIT | OTF_F_MEDI | OTF_F_FINA;
        for (int i = 0; i < r->len;) {
            int e = syl_end(r, i), t = SYL_TYPE(r->aux[i]);
            if (t == T_HIEROGLYPH || t == T_NON) last_form = -1;
            else {
                int join = last_form == 3 || last_form == 0;
                if (join) {
                    last_form = last_form == 3 ? 2 : 1;
                    for (int j = last_start; j < i; j++) r->mask[j] = (r->mask[j] & ~all) | forms[last_form];
                }
                last_form = join ? 3 : 0;
                for (int j = i; j < e; j++) r->mask[j] = (r->mask[j] & ~all) | forms[last_form];
            }
            last_start = i;
            i = e;
        }
    }
    free(cat);

    otf_substitute_stages(o, p, r, 0, 0);
    clear_subst(r);
    otf_substitute_stages(o, p, r, 1, 1);
    for (int i = 0; i < r->len;) {               /* un reph sustituido es R */
        int e = syl_end(r, i);
        for (int j = i; j < e && (r->mask[j] & OTF_F_RPHF); j++)
            if (r->aux[j] & OTF_AUX_SUBST) { set_cat(r, j, USE_R); break; }
        i = e;
    }
    clear_subst(r);
    otf_substitute_stages(o, p, r, 2, 2);
    for (int i = 0; i < r->len;) {               /* un pref sustituido se mueve como VPre */
        int e = syl_end(r, i);
        for (int j = i; j < e; j++) if (r->aux[j] & OTF_AUX_SUBST) { set_cat(r, j, USE_VPre); break; }
        i = e;
    }
    otf_substitute_stages(o, p, r, 3, 3);
    syl_dotted_circles(s, o, T_BROKEN, USE_B, USE_R, reserve);
    for (int i = 0; i < r->len;) {
        int e = syl_end(r, i);
        reorder(r, i, e);
        i = e;
    }
    otf_substitute_stages(o, p, r, 4, 255);
    return r->len;
}
