/* bpf2c.c — herramienta de compilación (corre en la máquina que compila, nunca como root): lee el
 * .o que arma `clang -target bpf` con np.bpf.c y escribe una cabecera C con las instrucciones de
 * cada programa y sus reubicaciones a mapas por nombre (build/gen/np_bpf.h). Así lord-netperm
 * lleva los programas adentro y no lee ni interpreta ELF en tiempo de ejecución.
 *
 *   build/bpf2c build/np.bpf.o build/gen/np_bpf.h
 *
 * Acepta solo lo que el cargador sabe resolver: secciones "cgroup/..." ejecutables, reubicaciones
 * R_BPF_64_64 contra símbolos de la sección "maps" en instrucciones ld_imm64, y nada en .text
 * (las funciones van inline) ni datos globales (.data/.rodata/.bss). Cualquier otra cosa es un
 * error: mejor fallar al compilar que cargar un programa a medias. */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EM_BPF
#define EM_BPF 247
#endif
#ifndef R_BPF_64_64
#define R_BPF_64_64 1
#endif

static unsigned char *buf;
static size_t len;

static int die(const char *m, const char *x) {
    fprintf(stderr, "bpf2c: %s%s%s\n", m, x ? ": " : "", x ? x : "");
    exit(1);
}

static const void *at(uint64_t off, uint64_t n) {
    if (off > len || n > len - off) die("fuera del archivo", NULL);
    return buf + off;
}

static const char *str(const Elf64_Shdr *sh, uint32_t off) {
    if (off >= sh->sh_size) die("cadena fuera de su tabla", NULL);
    const char *s = at(sh->sh_offset + off, 1);
    if (!memchr(s, 0, sh->sh_size - off)) die("cadena sin terminar", NULL);
    return s;
}

int main(int argc, char **argv) {
    if (argc != 3) die("uso: bpf2c <entrada.o> <salida.h>", NULL);
    FILE *f = fopen(argv[1], "rb");
    if (!f) die("no se pudo abrir", argv[1]);
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (l <= 0 || l > (64 << 20)) die("tamaño raro", argv[1]);
    len = (size_t)l;
    buf = malloc(len);
    if (!buf || fread(buf, 1, len, f) != len) die("no se pudo leer", argv[1]);
    fclose(f);

    const Elf64_Ehdr *eh = at(0, sizeof *eh);
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_type != ET_REL || eh->e_machine != EM_BPF || eh->e_shentsize != sizeof(Elf64_Shdr))
        die("no es un objeto BPF de 64 bits little endian", argv[1]);
    const Elf64_Shdr *sh = at(eh->e_shoff, (uint64_t)eh->e_shnum * sizeof *sh);
    if (eh->e_shstrndx >= eh->e_shnum) die("sin nombres de secciones", NULL);
    const Elf64_Shdr *shstr = &sh[eh->e_shstrndx];

    int symtab = -1, maps = -1;
    for (int i = 0; i < eh->e_shnum; i++) {
        const char *nm = str(shstr, sh[i].sh_name);
        if (sh[i].sh_type == SHT_SYMTAB) symtab = i;
        if (!strcmp(nm, "maps")) maps = i;
        if ((!strcmp(nm, ".text") || !strncmp(nm, ".data", 5) || !strncmp(nm, ".rodata", 7) || !strcmp(nm, ".bss")) && sh[i].sh_size)
            die("sección no admitida (funciones sin inline o datos globales)", nm);
    }
    if (symtab < 0 || maps < 0) die("faltan la tabla de símbolos o la sección maps", NULL);
    if (sh[symtab].sh_link >= eh->e_shnum || sh[symtab].sh_entsize != sizeof(Elf64_Sym)) die("tabla de símbolos rara", NULL);
    const Elf64_Shdr *strtab = &sh[sh[symtab].sh_link];
    const Elf64_Sym *syms = at(sh[symtab].sh_offset, sh[symtab].sh_size);
    size_t nsyms = sh[symtab].sh_size / sizeof *syms;

    FILE *o = fopen(argv[2], "w");
    if (!o) die("no se pudo escribir", argv[2]);
    fprintf(o, "/* np_bpf.h — generado por build/bpf2c desde np.bpf.c. No editar. */\n");
    fprintf(o, "#ifndef LORD_NP_BPF_GEN_H\n#define LORD_NP_BPF_GEN_H\n#include \"netperm/bpfsys.h\"\n\n");
    int nprog = 0;
    char names[32][64], fns[32][64];
    for (int i = 0; i < eh->e_shnum; i++) {
        const char *nm = str(shstr, sh[i].sh_name);
        if (sh[i].sh_type != SHT_PROGBITS || !(sh[i].sh_flags & SHF_EXECINSTR) || !sh[i].sh_size) continue;
        if (strncmp(nm, "cgroup/", 7)) die("sección ejecutable desconocida", nm);
        if (nprog >= 32 || strlen(nm) >= sizeof names[0]) die("demasiados programas", NULL);
        if (sh[i].sh_size % 8) die("tamaño de programa raro", nm);
        const char *fn = NULL;                          /* el símbolo de función de la sección */
        for (size_t k = 0; k < nsyms; k++)
            if (syms[k].st_shndx == i && ELF64_ST_TYPE(syms[k].st_info) == STT_FUNC) fn = str(strtab, syms[k].st_name);
        if (!fn || strlen(fn) >= sizeof fns[0]) die("programa sin nombre", nm);
        snprintf(names[nprog], sizeof names[0], "%s", nm);
        snprintf(fns[nprog], sizeof fns[0], "%s", fn);
        const uint64_t *ins = at(sh[i].sh_offset, sh[i].sh_size);
        size_t n = sh[i].sh_size / 8;
        fprintf(o, "static const uint64_t np_insns_%d[%zu] = {", nprog, n);
        for (size_t k = 0; k < n; k++) fprintf(o, "%s0x%016llxull,", k % 4 ? " " : "\n    ", (unsigned long long)ins[k]);
        fprintf(o, "\n};\n");
        /* reubicaciones de esta sección */
        int nrel = 0;
        fprintf(o, "static const NpReloc np_rel_%d[] = {\n", nprog);
        for (int r = 0; r < eh->e_shnum; r++) {
            if (sh[r].sh_info != (uint32_t)i) continue;
            if (sh[r].sh_type == SHT_RELA) die("reubicaciones RELA no admitidas", nm);
            if (sh[r].sh_type != SHT_REL) continue;
            if (sh[r].sh_entsize != sizeof(Elf64_Rel) || sh[r].sh_link != (uint32_t)symtab) die("tabla de reubicaciones rara", nm);
            const Elf64_Rel *rel = at(sh[r].sh_offset, sh[r].sh_size);
            for (size_t k = 0; k < sh[r].sh_size / sizeof *rel; k++) {
                uint64_t sym = ELF64_R_SYM(rel[k].r_info);
                if (ELF64_R_TYPE(rel[k].r_info) != R_BPF_64_64) die("reubicación no admitida (¿una llamada a una función sin inline?)", nm);
                if (sym >= nsyms || syms[sym].st_shndx != maps) die("reubicación contra algo que no es un mapa", nm);
                if (rel[k].r_offset % 8 || rel[k].r_offset / 8 + 1 >= n) die("reubicación fuera del programa", nm);
                uint64_t insn = ins[rel[k].r_offset / 8];
                if ((insn & 0xff) != 0x18) die("reubicación de mapa fuera de un ld_imm64", nm);
                const char *mn = str(strtab, syms[sym].st_name);
                for (const char *c = mn; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) die("nombre de mapa raro", mn);
                fprintf(o, "    { %llu, \"%s\" },\n", (unsigned long long)(rel[k].r_offset / 8), mn);
                nrel++;
            }
        }
        if (!nrel) fprintf(o, "    { 0, 0 },\n");
        fprintf(o, "};\n#define NP_NREL_%d %d\n\n", nprog, nrel);
        nprog++;
    }
    if (!nprog) die("no hay programas", NULL);
    fprintf(o, "static const NpProgDesc np_progs[] = {\n");
    for (int i = 0; i < nprog; i++)
        fprintf(o, "    { \"%s\", \"%s\", np_insns_%d, sizeof np_insns_%d / 8, np_rel_%d, NP_NREL_%d },\n", names[i], fns[i], i, i, i, i);
    fprintf(o, "};\n#define NP_NPROGS %d\n\n#endif\n", nprog);
    if (fclose(o)) die("no se pudo escribir", argv[2]);
    return 0;
}
