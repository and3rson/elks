#ifndef LX86_LINUXMT_MINIX_H
#define LX86_LINUXMT_MINIX_H

/*
 *	Minix binary formats
 */

#define RUNNABLE_HEADER_SIZE	32

/* Values for the type field of minix_exec_header */

#define MINIX_COMBID	0x04100301L
#define MINIX_SPLITID	0x04200301L
#define MINIX_S_SPLITID	0x04600301L
#define MINIX_DLLID	0x04A00301L

struct minix_exec_hdr {
    unsigned long	type;
    unsigned long	hlen;
    unsigned long	tseg;
    unsigned long	dseg;
    unsigned long	bseg;
    unsigned long	unused;
    unsigned long	chmem;
    unsigned long	unused2;
};

struct minix_supl_hdr {
    long		msh_trsize;	/* text relocation size */
    long		msh_drsize;	/* data relocation size */
    long		msh_tbase;	/* text relocation base */
    long		msh_dbase;	/* data relocation base */
};

#endif
