/*                          P I P E . C
 * BRL-CAD
 *
 * Copyright (c) 2004-2022 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file proc-db/pipe.c
 *
 * Generate piping (fuel, hydraulic lines, etc.) in MGED format from
 * input (points in space defining the routing).  Makes both tubing
 * regions and fluid regions or solid cable.  Automatically generates
 * elbow regions (and fluid in the elbows) when the piping changes
 * direction.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "bio.h"

#include "bu/app.h"
#include "bu/getopt.h"
#include "vmath.h"
#include "raytrace.h"
#include "wdb.h"


#define NAMESIZE 80		/* in v5, they can be longer than 16 */

#ifdef VERSION
#  undef VERSION
#endif
#define VERSION 3
#define RELEASE 1

#define REGION 1

#define MINR 2.		/* minimum bending radius is MINR*tubingradius */
/* note, the bending radius is measured to the */
/* edge of the tube, NOT the centerline of tube */

struct points
{
    point_t p;		/* data point */
    point_t p1;		/* adjusted point torward previous point */
    point_t p2;		/* adjusted point torward next point */
    point_t center;	/* center point for tori elbows */
    vect_t nprev;	/* unit vector towards previous point */
    vect_t nnext;	/* unit vector towards next point */
    fastf_t alpha;	/* angle between "nprev" and "nnext" */
    vect_t norm;	/* unit vector normal to "nprev" and "nnext" */
    vect_t nmitre;	/* unit vector along mitre joint */
    vect_t mnorm;	/* unit vector normal to mitre joint and "norm" */
    char tube[NAMESIZE], tubflu[NAMESIZE];			/* solid names */
    char elbow[NAMESIZE], elbflu[NAMESIZE], cut[NAMESIZE];	/* solid names */
    char tube_r[NAMESIZE], tubflu_r[NAMESIZE];		/* region names */
    char elbow_r[NAMESIZE], elbflu_r[NAMESIZE];		/* region names */
    struct points *next, *prev;
};


double radius, wall, pi, unit_conversion_factor;
struct points *root;
char name[16];
float delta=10.0;		/* 10mm excess in ARB sizing for cutting tubes */
mat_t identity;

int torus=0, sphere=0, mitre=0, nothing=0, cable=0;

/* formats for solid and region names */
char *tor_out="%02d-out.tor", *tor_in="%02d-in.tor";
char *haf="%02d.haf";
char *sph_out="%02d-out.sph", *sph_in="%02d-in.sph";
char *rcc_out="%02d-out.rcc", *rcc_in="%02d-in.rcc";
char *tub_reg="%02d.tube", *tub_flu="%02d.tubflu";
char *elb_reg="%02d.elbow", *elb_flu="%02d.elbflu";
char *arb="%02d.arb";

void Usage(void);

struct rt_wdb *fdout;	/* file for libwdb writes */

void
Make_name(char *ptr, const char *form, const char *base, int number)
{

    char scrat[NAMESIZE];
    size_t len;

    bu_strlcpy(ptr, base, NAMESIZE);
    snprintf(scrat, NAMESIZE, form, number);

    len = strlen(ptr) + strlen(scrat);
    if (len > (NAMESIZE-1))
	ptr[ (NAMESIZE-1) - strlen(scrat) ] = '\0';

    bu_strlcat(ptr, scrat, NAMESIZE);
}


void
Readpoints(void)
{
    struct points *ptr, *prev;
    double x, y, z;

    ptr = root;
    prev = NULL;


    printf("X Y Z (^D for end): ");
    while (scanf("%lf%lf%lf", &x, &y, &z) ==  3) {
	if (ptr == NULL) {
	    BU_ALLOC(ptr, struct points);
	    root = ptr;
	} else {
	    BU_ALLOC(ptr->next, struct points);
	    ptr = ptr->next;
	}
	ptr->next = NULL;
	ptr->prev = prev;
	prev = ptr;
	VSET(ptr->p, unit_conversion_factor*x, unit_conversion_factor*y, unit_conversion_factor*z);
	ptr->tube[0] = '\0';
	ptr->tubflu[0] = '\0';
	ptr->elbow[0] = '\0';
	ptr->elbflu[0] = '\0';
	ptr->cut[0] = '\0';
	printf("X Y Z (^D for end): ");
    }
}


void
Names(void)
{
    struct points *ptr;
    char *inform=NULL, *outform=NULL, *cutform=NULL;
    int nummer=0;


    if (torus) {
	outform = tor_out;
	inform = tor_in;
	cutform = arb;
    } else if (sphere) {
	outform = sph_out;
	inform = sph_in;
    } else if (mitre)
	cutform = haf;

    ptr = root;
    while (ptr->next != NULL) {
	nummer++;

	/* Outer RCC */
	Make_name(ptr->tube, rcc_out, name, nummer);

	if (!cable) /* Inner RCC */
	    Make_name(ptr->tubflu, rcc_in, name, nummer);

	if ((sphere || torus) && ptr != root) {
	    /* Outer elbow */
	    Make_name(ptr->elbow, outform, name, nummer);

	    if (!cable) /* Inner elbow */
		Make_name(ptr->elbflu, inform, name, nummer);
	}
	if ((torus || mitre) && !sphere)	/* Make cutting solid name */
	    Make_name(ptr->cut, cutform, name, nummer);

	/* Make region names */
	Make_name(ptr->tube_r, tub_reg, name, nummer); /* tube region */

	if ((torus || sphere) && ptr != root)	/* Make elbow region name */
	    Make_name(ptr->elbow_r, elb_reg, name, nummer);

	/* Make fluid region names */
	if (!cable) {
	    Make_name(ptr->tubflu_r, tub_flu, name, nummer);

	    if ((torus || sphere) && ptr != root)	/* Make elbow fluid region names */
		Make_name(ptr->elbflu_r, elb_flu, name, nummer);
	}

	ptr = ptr->next;
    }
    ptr->tube[0] = '\0';
    ptr->tubflu[0] = '\0';
    ptr->elbow[0] = '\0';
    ptr->elbflu[0] = '\0';
    ptr->cut[0] = '\0';
    ptr->tube_r[0] = '\0';
    ptr->tubflu_r[0] = '\0';
    ptr->elbow_r[0] = '\0';
    ptr->elbflu_r[0] = '\0';
}


void
Normals(void)
{
    struct points *ptr;


    if (root == NULL)
	return;

    ptr = root->next;
    if (ptr == NULL)
	return;

    VSUB2(root->nnext, ptr->p, root->p);
    VUNITIZE(root->nnext);

    while (ptr->next != NULL) {
	VREVERSE(ptr->nprev, ptr->prev->nnext);
	VSUB2(ptr->nnext, ptr->next->p, ptr->p);
	VUNITIZE(ptr->nnext);
	VCROSS(ptr->norm, ptr->nprev, ptr->nnext);
	VUNITIZE(ptr->norm);
	VADD2(ptr->nmitre, ptr->nprev, ptr->nnext);
	VUNITIZE(ptr->nmitre);
	VCROSS(ptr->mnorm, ptr->norm, ptr->nmitre);
	VUNITIZE(ptr->mnorm);
	if (VDOT(ptr->mnorm, ptr->nnext) > 0.0)
	    VREVERSE(ptr->mnorm, ptr->mnorm);
	ptr->alpha = acos(VDOT(ptr->nnext, ptr->nprev));
	ptr = ptr->next;
    }
}


void
Adjust(void)
{
    fastf_t beta, d, len;
    struct points *ptr;

    if (root == NULL)
	return;

    VMOVE(root->p1, root->p);
    VMOVE(root->p2, root->p);

    ptr = root->next;
    if (ptr == NULL)
	return;

    if (ptr->next == NULL) {
	VMOVE(ptr->p1, ptr->p);
	VMOVE(ptr->p2, ptr->p);
	return;
    }


    while (ptr->next != NULL) {
	if (!torus && !mitre) {
	    VMOVE(ptr->p1, ptr->p);
	    VMOVE(ptr->p2, ptr->p);
	} else if (torus) {
	    /* beta=.5*(pi-ptr->alpha);
	       d=(MINR+1.0)*radius*tan(beta); */ /* dist from new endpts to p2 */

	    beta = 0.5 * ptr->alpha;
	    d = (MINR + 1.0) * radius / tan(beta);
	    VJOIN1(ptr->p1, ptr->p, d, ptr->nprev);
	    VJOIN1(ptr->p2, ptr->p, d, ptr->nnext);
	    d = sqrt(d*d + (MINR+1.0)*(MINR+1.)*radius*radius);
	    VJOIN1(ptr->center, ptr->p, d, ptr->nmitre);
	} else if (mitre) {
	    len = radius/tan(ptr->alpha/2.0);
	    VJOIN1(ptr->p1, ptr->p, -len, ptr->nprev);
	    VJOIN1(ptr->p2, ptr->p, -len, ptr->nnext);
	}
	ptr = ptr->next;
    }
    VMOVE(ptr->p1, ptr->p);
    VMOVE(ptr->p2, ptr->p);
}


void
Pipes(void)
{
    vect_t ht;
    struct points *ptr;
    fastf_t len;
    int comblen;
    struct wmember head;

    BU_LIST_INIT(&head.l);

    ptr = root;
    if (ptr == NULL)
	return;

    while (ptr->next != NULL) {

	/* Make the basic pipe solids */

	VSUB2(ht, ptr->next->p1, ptr->p2);

	mk_rcc(fdout, ptr->tube, ptr->p2, ht, radius);	/* make a solid record */

	if (!cable) /* make inside solid */
	    mk_rcc(fdout, ptr->tubflu, ptr->p2, ht, radius-wall);

	if (torus) {
	    /* Make tubing region */
	    mk_addmember(ptr->tube, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */

	    if (!cable) {
		/* Subtract inside solid */
		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_SUBTRACT);	/* make '-' member record */

		/* Make fluid region */
		mk_comb1(fdout, ptr->tubflu_r, ptr->tubflu, 1);
	    }
	    mk_lfcomb(fdout, ptr->tube_r, &head, 1);
	} else if (mitre) {
	    if (ptr->prev != NULL) {
		len = VDOT(ptr->p, ptr->mnorm);
		mk_half(fdout, ptr->cut, ptr->mnorm, len);
	    }

	    comblen = 4 - cable;
	    if (ptr->next->next == NULL)
		comblen--;
	    if (ptr->prev == NULL)
		comblen--;

	    mk_addmember(ptr->tube, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */

	    if (!cable)
		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_SUBTRACT);	/* make '-' member record */

	    if (ptr->next->next != NULL)
		mk_addmember(ptr->next->cut, &head.l, NULL, WMOP_SUBTRACT);	/* make '+' member record */

	    if (ptr->prev != NULL)
		mk_addmember(ptr->cut, &head.l, NULL, WMOP_INTERSECT);	/* subtract HAF */

	    mk_lfcomb(fdout, ptr->tube_r, &head, 1);

	    if (!cable) {
		/* Make fluid region */

		comblen = 3;
		if (ptr->next->next == NULL)
		    comblen--;
		if (ptr->prev == NULL)
		    comblen--;

		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */

		if (ptr->next->next != NULL)
		    mk_addmember(ptr->next->cut, &head.l, NULL, WMOP_SUBTRACT);	/* make '+' member record */

		if (ptr->prev != NULL)
		    mk_addmember(ptr->cut, &head.l, NULL, WMOP_INTERSECT);	/* subtract */

		mk_lfcomb(fdout, ptr->tubflu_r, &head, 1);	/* make REGION comb record */
	    }
	} else if (sphere) {

	    /* make REGION comb record for tube */
	    comblen = 2;
	    if (cable)
		comblen--;
	    if (ptr->next->tube[0] != '\0')
		comblen++;
	    if (!cable && ptr->prev != NULL)
		comblen++;

	    /* make 'u' member record */

	    mk_addmember(ptr->tube, &head.l, NULL, WMOP_UNION);

	    /* make '-' member record */
	    if (!cable)
		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_SUBTRACT);

	    if (ptr->next->tube[0] != '\0')	/* subtract outside of next tube */
		mk_addmember(ptr->next->tube, &head.l, NULL, WMOP_SUBTRACT);
	    if (!cable && ptr->prev != NULL) /* subtract inside of previous tube */
		mk_addmember(ptr->prev->tubflu, &head.l, NULL, WMOP_SUBTRACT);

	    mk_lfcomb(fdout, ptr->tube_r, &head, REGION);

	    if (!cable) {
		/* make REGION for fluid */

		/* make 'u' member record */

		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_UNION);

		if (ptr->next->tubflu[0] != '\0')	/* subtract inside of next tube */
		    mk_addmember(ptr->next->tubflu, &head.l, NULL, WMOP_SUBTRACT);

		mk_lfcomb(fdout, ptr->tubflu_r, &head, REGION);
	    }
	} else if (nothing) {

	    /* make REGION comb record for tube */
	    comblen = 2;
	    if (cable)
		comblen--;
	    if (ptr->next->tube[0] != '\0')
		comblen++;
	    if (!cable && ptr->prev != NULL)
		comblen++;

	    /* make 'u' member record */

	    mk_addmember(ptr->tube, &head.l, NULL, WMOP_UNION);

	    /* make '-' member record */
	    if (!cable)
		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_SUBTRACT);

	    if (ptr->next->tube[0] != '\0')	/* subtract outside of next tube */
		mk_addmember(ptr->next->tube, &head.l, NULL, WMOP_SUBTRACT);
	    if (!cable && ptr->prev != NULL) /* subtract inside of previous tube */
		mk_addmember(ptr->prev->tubflu, &head.l, NULL, WMOP_SUBTRACT);

	    mk_lfcomb(fdout, ptr->tube_r, &head, REGION);

	    if (!cable) {
		/* make REGION for fluid */

		/* make 'u' member record */

		mk_addmember(ptr->tubflu, &head.l, NULL, WMOP_UNION);

		if (ptr->next->tubflu[0] != '\0')	/* subtract inside of next tube */
		    mk_addmember(ptr->next->tubflu, &head.l, NULL, WMOP_SUBTRACT);

		mk_lfcomb(fdout, ptr->tubflu_r, &head, REGION);
	    }
	}
	ptr = ptr->next;
    }
}


void
Elbows(void)	/* make a tubing elbow and fluid elbow */
{
    vect_t RN1, RN2;
    point_t pts[8];
    fastf_t len;
    struct points *ptr;
    struct wmember head;

    if (nothing || mitre)
	return;

    if (root == NULL)
	return;

    BU_LIST_INIT(&head.l);

    ptr = root->next;
    while (ptr->next != NULL) {

	/* Make outside elbow solid */
	if (torus)
	    mk_tor(fdout, ptr->elbow, ptr->center, ptr->norm, (MINR+1)*radius, radius);
	else if (sphere)
	    mk_sph(fdout, ptr->elbow, ptr->p, radius);

	/* Make inside elbow solid */
	if (!cable) {
	    if (torus)
		mk_tor(fdout, ptr->elbflu, ptr->center, ptr->norm, (MINR+1)*radius, radius-wall);
	    else if (sphere)
		mk_sph(fdout, ptr->elbflu, ptr->p, radius-wall);
	}

	/* Make ARB8 solid */
	if (torus) {
	    len = ((MINR+2)*radius + delta) / cos((pi-ptr->alpha)/4.0);
	    /* vector from center of torus to rcc end */
	    VSUB2(RN1, ptr->p1, ptr->center);
	    VUNITIZE(RN1);		/* unit vector */
	    /* beginning of next rcc */
	    VSUB2(RN2, ptr->p2, ptr->center);
	    VUNITIZE(RN2);		/* and unitize again */

	    /* build the eight points for the ARB8 */

	    VJOIN1(pts[0], ptr->center, radius+delta, ptr->norm);
	    VJOIN1(pts[1], pts[0], len, RN1);
	    VJOIN1(pts[2], pts[0], -len, ptr->nmitre);
	    VJOIN1(pts[3], pts[0], len, RN2);
	    VJOIN1(pts[4], ptr->center, -radius-delta, ptr->norm);
	    VJOIN1(pts[5], pts[4], len, RN1);
	    VJOIN1(pts[6], pts[4], -len, ptr->nmitre);
	    VJOIN1(pts[7], pts[4], len, RN2);

	    mk_arb8(fdout, ptr->cut, &pts[0][X]);
	}

	if (torus) {
	    mk_addmember(ptr->elbow, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */
	    if (!cable)
		mk_addmember(ptr->elbflu, &head.l, NULL, WMOP_SUBTRACT);	/* make '-' member record */
	    mk_addmember(ptr->cut, &head.l, NULL, WMOP_INTERSECT);
	    mk_lfcomb(fdout, ptr->elbow_r, &head, REGION);	/* make REGION comb record */

	    if (!cable) {
		mk_addmember(ptr->elbflu, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */
		mk_addmember(ptr->cut, &head.l, NULL, WMOP_INTERSECT);
		mk_lfcomb(fdout, ptr->elbflu_r, &head, REGION);		/* make REGION comb record */
	    }
	} else if (sphere) {
	    mk_addmember(ptr->elbow, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */
	    if (!cable)
		mk_addmember(ptr->elbflu, &head.l, NULL, WMOP_SUBTRACT);	/* make '-' member record */
	    mk_addmember(ptr->tube, &head.l, NULL, WMOP_SUBTRACT);
	    mk_addmember(ptr->prev->tube, &head.l, NULL, WMOP_SUBTRACT);

	    mk_lfcomb(fdout, ptr->elbow_r, &head, REGION);	/* make REGION comb record */

	    if (!cable) {
		mk_addmember(ptr->elbflu, &head.l, NULL, WMOP_UNION);	/* make 'u' member record */
		mk_addmember(ptr->tube, &head.l, NULL, WMOP_SUBTRACT);
		mk_addmember(ptr->prev->tube, &head.l, NULL, WMOP_SUBTRACT);
		mk_lfcomb(fdout, ptr->elbflu_r, &head, REGION);		/* make REGION comb record */
	    }
	}
	ptr = ptr->next;
    }
}


void
Groups(void)
{
    struct points *ptr;
    char tag[NAMESIZE];
    char *pipe_group=".pipe";
    char *fluid_group=".fluid";
    int comblen=0;
    struct wmember head;

    BU_LIST_INIT(&head.l);

    ptr = root;
    if (ptr == NULL)
	return;

    while (ptr->next != NULL) {
	if (!nothing && !mitre && comblen)	/* count elbow sections (except first point) */
	    comblen++;
	comblen++;	/* count pipe sections */

	ptr = ptr->next;
    }

    if (comblen) {

	/* Make name for pipe group = "name".pipe */
	Make_name(tag, pipe_group, name, 0);

	/* Make group */
	ptr = root;
	while (ptr->next != NULL) {
	    mk_addmember(ptr->tube_r, &head.l, NULL, WMOP_UNION);	/* tube regions */

	    if (!nothing && !mitre && ptr != root)
		mk_addmember(ptr->elbow_r, &head.l, NULL, WMOP_UNION);	/* elbows */

	    ptr = ptr->next;
	}
	mk_lfcomb(fdout, tag, &head, 0);

	if (!cable) {
	    /* Make name for fluid group = "name".fluid */
	    Make_name(tag, fluid_group, name, 0);

	    /* Make group */
	    ptr = root;
	    while (ptr->next != NULL) {
		mk_addmember(ptr->tubflu_r, &head.l, NULL, WMOP_UNION);	/* fluid in tubes */

		if (!nothing && !mitre && ptr != root)
		    mk_addmember(ptr->elbflu_r, &head.l, NULL, WMOP_UNION); /* fluid in elbows */

		ptr = ptr->next;
	    }
	    mk_lfcomb(fdout, tag, &head, 0);
	}
    }
}


int
main(int argc, char **argv)
{
    int done = 0;
    char units[16], fname[80];
    int optc;

    bu_setprogname(argv[0]);

    while ((optc = bu_getopt(argc, argv, "tsmnch?")) != -1) {
	/* Set joint type and cable option */
	switch (optc) {
	    case 't':
		torus = 1;
		break;
	    case 's':
		sphere = 1;
		break;
	    case 'm':
		mitre = 1;
		break;
	    case 'n':
		nothing = 1;
		break;
	    case 'c':
		cable = 1;
		break;
	    default:
		Usage();
		return 1;

	}
    }

    /* Too many joint options */
    if ((torus + sphere + mitre + nothing) > 1) {
	Usage();
	fprintf(stderr, "Options t, s, m, n are mutually exclusive\n");
	return 1;
    }

    if ((argc - bu_optind) != 2) {
	Usage();
	return 1;
    }

    if ((torus + sphere + mitre + nothing) == 0)
	torus = 1;		/* default */

    bu_strlcpy(name, argv[bu_optind++], sizeof(name)); /* Base name for objects */

    fdout = wdb_fopen(argv[bu_optind]);
    if (fdout == NULL) {
	fprintf(stderr, "Cannot open %s\n", argv[bu_optind]);
	perror("Pipe");
	Usage();
	return 1;
    }

    MAT_IDN(identity);	/* Identity matrix for all objects */
    pi = M_PI;	/* PI */

    printf("FLUID & PIPING V%d.%d 10 Mar 89\n\n", VERSION, RELEASE);
    printf("Append %s to your target description using 'concat' in mged.\n", argv[bu_optind]);

#define MM_TO_CM 10.0
#define MM_TO_M  1000.0
#define MM_TO_IN 25.4
#define MM_TO_FT 304.8

    unit_conversion_factor = 0.0;
    while (ZERO(unit_conversion_factor)) {
	printf("UNITS? (ft, in, m, cm, default is millimeters) ");
	bu_fgets(units, sizeof(units), stdin);
	switch (units[0]) {
	    case '\0':
	    case '\n':
	    case '\r':
		unit_conversion_factor=1.0;
		break;

	    case 'f':
		unit_conversion_factor=MM_TO_FT;
		break;

	    case 'i':
		unit_conversion_factor=MM_TO_IN;
		break;

	    case 'm':
		if (units[1] != 'm')
		    unit_conversion_factor=MM_TO_M;
		else
		    unit_conversion_factor=1.0;
		break;

	    case 'c':
		unit_conversion_factor=MM_TO_CM;
		break;

	    default:
		printf("\n%s is not a legal choice for units\n", units);
		printf("Try again\n");
		break;
	}
    }

    done = 0;
    while (!done) {
	if (!cable) {
	    printf("radius and wall thickness: ");
	    if (scanf("%lf %lf", &radius, &wall) == EOF)
		return 1;
	    if (radius > wall)
		done = 1;
	    else {
		printf(" *** bad input!\n\n");
		printf("radius must be larger than wall thickness\n");
		printf("Try again\n");
	    }
	} else {
	    printf("radius: ");
	    if (scanf("%lf", &radius) == EOF)
		return 1;
	    done=1;
	}
    }
    if (radius < SMALL_FASTF)
	radius = SMALL_FASTF;

    radius=unit_conversion_factor*radius;
    wall=unit_conversion_factor*wall;

    Readpoints();	/* Read data points */

    Names();	/* Construct names for all solids */

    Normals();	/* Calculate normals and other vectors */

    Adjust();       /* Adjust points to allow for elbows */

/* Generate Title */

    bu_strlcpy(fname, name, sizeof(fname));

    if (!cable)
	bu_strlcat(fname, " pipe and fluid", sizeof(fname));
    else
	bu_strlcat(fname, " cable", sizeof(fname));

/* Create ident record */

    mk_id(fdout, fname);

    Pipes();	/* Construct the piping */

    Elbows();	/* Construct the elbow sections */

    Groups();	/* Make some groups */

    return 0;
}


void
Usage(void)
{
    fprintf(stderr, "Usage: pipe [-tsmnc] tag filename\n");
    fprintf(stderr, "   where 'tag' is the name of the piping run and is used by mged in object names;\n");
    fprintf(stderr, "   and 'filename' is the .g file (e.g., fuel.g)\n");
    fprintf(stderr, "   -t -> use tori at the bends (default)\n");
    fprintf(stderr, "   -s -> use spheres at the corners\n");
    fprintf(stderr, "   -m -> mitre the corners\n");
    fprintf(stderr, "   -n -> nothing at the corners\n");
    fprintf(stderr, "   -c -> cable (no fluid)\n");
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
