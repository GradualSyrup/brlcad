// This evolved from the original trimesh halfedge data structure code:

// Author: Yotam Gingold <yotam (strudel) yotamgingold.com>
// License: Public Domain.  (I, Yotam Gingold, the author, release this code into the public domain.)
// GitHub: https://github.com/yig/halfedge
//
// It ended up rather different, as we are concerned with somewhat different
// mesh properties for CDT and the halfedge data structure didn't end up being
// a good fit, but as it's derived from that source the public domain license
// is maintained for the changes as well.

#include "common.h"

#include "bu/color.h"
#include "bu/sort.h"
#include "bu/malloc.h"
//#include "bn/mat.h" /* bn_vec_perp */
#include "bn/plot3.h"
#include "bg/polygon.h"
#include "./cmesh.h"

// needed for implementation
#include <iostream>
#include <stack>

namespace cmesh
{

long
cpolygon_t::add_edge(const struct edge_t &e)
{
    if (e.v[0] == -1) return -1;

    int v1 = -1;
    int v2 = -1;

    std::set<cpolyedge_t *>::iterator cp_it;
    for (cp_it = poly.begin(); cp_it != poly.end(); cp_it++) {
	cpolyedge_t *pe = *cp_it;

	if (pe->v[1] == e.v[0]) {
	    v1 = e.v[0];
	}

	if (pe->v[1] == e.v[1]) {
	    v1 = e.v[1];
	}

	if (pe->v[0] == e.v[0]) {
	    v2 = e.v[0];
	}

	if (pe->v[0] == e.v[1]) {
	    v2 = e.v[1];
	}
    }

    if (v1 == -1) {
	v1 = (e.v[0] == v2) ? e.v[1] : e.v[0];
    }

    if (v2 == -1) {
	v2 = (e.v[0] == v1) ? e.v[1] : e.v[0];
    }

    struct edge_t le(v1, v2);
    cpolyedge_t *nedge = new cpolyedge_t(le);
    poly.insert(nedge);

    v2pe[v1].insert(nedge);
    v2pe[v2].insert(nedge);
    active_edges.insert(uedge_t(le));

    cpolyedge_t *prev = NULL;
    cpolyedge_t *next = NULL;

    for (cp_it = poly.begin(); cp_it != poly.end(); cp_it++) {
	cpolyedge_t *pe = *cp_it;

	if (pe == nedge) continue;

	if (pe->v[1] == nedge->v[0]) {
	    prev = pe;
	}

	if (pe->v[0] == nedge->v[1]) {
	    next = pe;
	}
    }

    if (prev) {
	prev->next = nedge;
	nedge->prev = prev;
    }

    if (next) {
	next->prev = nedge;
	nedge->next = next;
    }


    return 0;
}

void
cpolygon_t::remove_edge(const struct edge_t &e)
{
    struct uedge_t ue(e);
    cpolyedge_t *cull = NULL;
    std::set<cpolyedge_t *>::iterator cp_it;
    for (cp_it = poly.begin(); cp_it != poly.end(); cp_it++) {
	cpolyedge_t *pe = *cp_it;
	struct uedge_t pue(pe->v[0], pe->v[1]);
	if (ue == pue) {
	    // Existing segment with this ending vertex exists
	    cull = pe;
	    break;
	}
    }

    if (!cull) return;

    v2pe[e.v[0]].erase(cull);
    v2pe[e.v[1]].erase(cull);
    active_edges.erase(ue);

    // An edge removal may produce a new interior point candidate - check
    // Will need to verify eventually with point_in_polygon, but topologically
    // it may be cut loose
    for (int i = 0; i < 2; i++) {
	if (!v2pe[e.v[i]].size()) {
	    if (flipped_face.find(e.v[i]) != flipped_face.end()) {
		flipped_face.erase(e.v[i]);
	    }
	    uncontained.insert(e.v[i]);
	}
    }

    for (cp_it = poly.begin(); cp_it != poly.end(); cp_it++) {
	cpolyedge_t *pe = *cp_it;
	if (pe->prev == cull) {
	    pe->prev = NULL;
	}
	if (pe->next == cull) {
	    pe->next = NULL;
	}
    }
    poly.erase(cull);
    delete cull;
}

long
cpolygon_t::replace_edges(std::set<edge_t> &new_edges, std::set<edge_t> &old_edges)
{

    std::set<edge_t>::iterator e_it;
    for (e_it = old_edges.begin(); e_it != old_edges.end(); e_it++) {
	remove_edge(*e_it);
    }
    for (e_it = new_edges.begin(); e_it != new_edges.end(); e_it++) {
	add_edge(*e_it);
    }

    return 0;
}

long
cpolygon_t::shared_edge_cnt(triangle_t &t)
{
    struct uedge_t ue[3];
    ue[0].set(t.v[0], t.v[1]);
    ue[1].set(t.v[1], t.v[2]);
    ue[2].set(t.v[2], t.v[0]);
    long shared_cnt = 0;
    for (int i = 0; i < 3; i++) {
	if (active_edges.find(ue[i]) != active_edges.end()) {
	    shared_cnt++;
	}
    }
    return shared_cnt;
}

long
cpolygon_t::unshared_vertex(triangle_t &t)
{
    if (shared_edge_cnt(t) != 1) return -1;

    for (int i = 0; i < 3; i++) {
	if (v2pe.find(t.v[i]) == v2pe.end()) {
	    return t.v[i];
	}
    }

    return -1;
}

std::pair<long,long>
cpolygon_t::shared_vertices(triangle_t &t)
{
    if (shared_edge_cnt(t) != 1) {
       	return std::make_pair<long,long>(-1,-1);
    }

    std::pair<long, long> ret;

    int vcnt = 0;
    for (int i = 0; i < 3; i++) {
	if (v2pe.find(t.v[i]) != v2pe.end()) {
	   if (!vcnt) {
	       ret.first = t.v[i];
	       vcnt++;
	   } else {
	       ret.second = t.v[i];
	   }
	}
    }

    return ret;
}

double
cpolygon_t::ucv_angle(triangle_t &t)
{
    double r_ang = DBL_MAX;
    std::set<long>::iterator u_it;
    long nv = unshared_vertex(t);
    if (nv == -1) return -1;
    std::pair<long, long> s_vert = shared_vertices(t);
    if (s_vert.first == -1 || s_vert.second == -1) return -1;

    ON_3dPoint ep1 = ON_3dPoint(pnts_2d[s_vert.first][X], pnts_2d[s_vert.first][Y], 0);
    ON_3dPoint ep2 = ON_3dPoint(pnts_2d[s_vert.second][X], pnts_2d[s_vert.second][Y], 0);
    ON_3dPoint pnew = ON_3dPoint(pnts_2d[nv][X], pnts_2d[nv][Y], 0);
    ON_Line l2d(ep1,ep2);
    ON_3dPoint pline = l2d.ClosestPointTo(pnew);
    ON_3dVector vu = pnew - pline;
    vu.Unitize();

    for (u_it = uncontained.begin(); u_it != uncontained.end(); u_it++) {
	if (point_in_polygon(*u_it, true)) {
	    ON_2dPoint op = ON_2dPoint(pnts_2d[*u_it][X], pnts_2d[*u_it][Y]);
	    ON_3dVector vt = op - pline;
	    vt.Unitize();
	    double vangle = ON_DotProduct(vu, vt);
	    if (vangle > 0 && r_ang > vangle) {
		r_ang = vangle;
	    }
	}
    }
    for (u_it = flipped_face.begin(); u_it != flipped_face.end(); u_it++) {
	if (point_in_polygon(*u_it, true)) {
	    ON_2dPoint op = ON_2dPoint(pnts_2d[*u_it][X], pnts_2d[*u_it][Y]);
	    ON_3dVector vt = op - pline;
	    vt.Unitize();
	    double vangle = ON_DotProduct(vu, vt);
	    if (vangle > 0 && r_ang > vangle) {
		r_ang = vangle;
	    }
	}
    }

    return r_ang;
}


bool
cpolygon_t::self_intersecting()
{
    self_isect_edges.clear();
    bool self_isect = false;
    std::map<long, int> vecnt;
    std::set<cpolyedge_t *>::iterator pe_it;
    for (pe_it = poly.begin(); pe_it != poly.end(); pe_it++) {
	cpolyedge_t *pe = *pe_it;
	vecnt[pe->v[0]]++;
	vecnt[pe->v[1]]++;
    }
    std::map<long, int>::iterator v_it;
    for (v_it = vecnt.begin(); v_it != vecnt.end(); v_it++) {
	if (v_it->second > 2) {
	    self_isect = true;
	    if (!cmesh->brep_edge_pnt(v_it->second)) {
		uncontained.insert(v_it->second);
	    }
	}
    }

    // Check the projected segments against each other as well.  Store any
    // self-intersecting edges for use in later repair attempts.
    std::vector<cpolyedge_t *> pv(poly.begin(), poly.end());
    for (size_t i = 0; i < pv.size(); i++) {
	cpolyedge_t *pe1 = pv[i];
	ON_2dPoint p1_1(pnts_2d[pe1->v[0]][X], pnts_2d[pe1->v[0]][Y]);
	ON_2dPoint p1_2(pnts_2d[pe1->v[1]][X], pnts_2d[pe1->v[1]][Y]);
	struct uedge_t ue1(pe1->v[0], pe1->v[1]);
	// if we already know this segment intersects at least one other segment, we
	// don't need to re-test it - it's already "active"
	if (self_isect_edges.find(ue1) != self_isect_edges.end()) continue;
	ON_Line e1(p1_1, p1_2);
	for (size_t j = i+1; j < pv.size(); j++) {
	    cpolyedge_t *pe2 = pv[j];
	    ON_2dPoint p2_1(pnts_2d[pe2->v[0]][X], pnts_2d[pe2->v[0]][Y]);
	    ON_2dPoint p2_2(pnts_2d[pe2->v[1]][X], pnts_2d[pe2->v[1]][Y]);
	    struct uedge_t ue2(pe2->v[0], pe2->v[1]);
	    ON_Line e2(p2_1, p2_2);

	    double a, b = 0;
	    if (!ON_IntersectLineLine(e1, e2, &a, &b, 0.0, false)) {
		continue;
	    }

	    if ((a < 0 || NEAR_ZERO(a, ON_ZERO_TOLERANCE) || a > 1 || NEAR_ZERO(1-a, ON_ZERO_TOLERANCE)) ||
		    (b < 0 || NEAR_ZERO(b, ON_ZERO_TOLERANCE) || b > 1 || NEAR_ZERO(1-b, ON_ZERO_TOLERANCE))) {
		continue;
	    } else {
		self_isect_edges.insert(ue1);
		self_isect_edges.insert(ue2);
	    }

	    self_isect = true;
	}
    }

    return self_isect;
}

bool
cpolygon_t::closed()
{
    if (poly.size() < 3) {
	return false;
    }

    if (flipped_face.size()) {
	return false;
    }

    if (self_intersecting()) {
	return false;
    }

    size_t ecnt = 1;
    cpolyedge_t *pe = (*poly.begin());
    cpolyedge_t *first = pe;
    cpolyedge_t *next = pe->next;

    // Walk the loop - an infinite loop is not closed
    while (first != next) {
	ecnt++;
	next = next->next;
	if (ecnt > poly.size()) {
	    return false;
	}
    }

    // If we're not using all the poly edges in the loop, we're not closed
    if (ecnt < poly.size()) {
	return false;
    }

    // Prev and next need to be set for all poly edges, or we're not closed
    std::set<cpolyedge_t *>::iterator cp_it;
    for (cp_it = poly.begin(); cp_it != poly.end(); cp_it++) {
	cpolyedge_t *pec = *cp_it;
	if (!pec->prev || !pec->next) {
	    return false;
	}
    }

    return true;
}

bool
cpolygon_t::point_in_polygon(long v, bool flip)
{
    if (v == -1) return false;
    if (!closed()) return false;

    point2d_t *polypnts = (point2d_t *)bu_calloc(poly.size()+1, sizeof(point2d_t), "polyline");

    size_t pind = 0;

    cpolyedge_t *pe = (*poly.begin());
    cpolyedge_t *first = pe;
    cpolyedge_t *next = pe->next;

    if (v == pe->v[0] || v == pe->v[1]) {
	bu_free(polypnts, "polyline");
	return false;
    }

    V2MOVE(polypnts[pind], pnts_2d[pe->v[0]]);
    pind++;
    V2MOVE(polypnts[pind], pnts_2d[pe->v[1]]);

    // Walk the loop
    while (first != next) {
	pind++;
	if (v == next->v[0] || v == next->v[1]) {
	    bu_free(polypnts, "polyline");
	    return false;
	}
	V2MOVE(polypnts[pind], pnts_2d[next->v[1]]);
	next = next->next;
    }

#if 0
    if (bg_polygon_direction(pind+1, pnts_2d, NULL) == BG_CCW) {
	point2d_t *rpolypnts = (point2d_t *)bu_calloc(poly.size()+1, sizeof(point2d_t), "polyline");
	for (long p = (long)pind; p >= 0; p--) {
	    V2MOVE(rpolypnts[pind - p], polypnts[p]);
	}
	bu_free(polypnts, "free original loop");
	polypnts = rpolypnts;
    }
#endif

    //bg_polygon_plot_2d("bg_pnt_in_poly_loop.plot3", polypnts, pind, 255, 0, 0);

    point2d_t test_pnt;
    V2MOVE(test_pnt, pnts_2d[v]);

    bool result = (bool)bg_pt_in_polygon(pind, (const point2d_t *)polypnts, (const point2d_t *)&test_pnt);

    if (flip) {
	result = (result) ? false : true;
    }

    bu_free(polypnts, "polyline");

    return result;
}

bool
cpolygon_t::cdt()
{
    if (!closed()) return false;
    int *faces = NULL;
    int num_faces = 0;
    int *steiner = NULL;
    if (interior_points.size()) {
	steiner = (int *)bu_calloc(interior_points.size(), sizeof(int), "interior points");
	std::set<long>::iterator p_it;
	int vind = 0;
	for (p_it = interior_points.begin(); p_it != interior_points.end(); p_it++) {
	    steiner[vind] = (int)*p_it;
	    vind++;
	}
    }

    int *opoly = (int *)bu_calloc(poly.size()+1, sizeof(int), "polygon points");

    size_t vcnt = 1;
    cpolyedge_t *pe = (*poly.begin());
    cpolyedge_t *first = pe;
    cpolyedge_t *next = pe->next;

    opoly[vcnt-1] = pe->v[0];
    opoly[vcnt] = pe->v[1];

    // Walk the loop
    while (first != next) {
	vcnt++;
	opoly[vcnt] = next->v[1];
	next = next->next;
    	if (vcnt > poly.size()) {
	    std::cerr << "cdt attempt on infinite loop (shouldn't be possible - closed() test failed to detect this somehow...)\n";
	    return false;
	}
    }

    bool result = (bool)!bg_nested_polygon_triangulate( &faces, &num_faces,
	    NULL, NULL, opoly, poly.size()+1, NULL, NULL, 0, steiner,
	    interior_points.size(), pnts_2d, cmesh->pnts.size(),
	    TRI_CONSTRAINED_DELAUNAY);

    if (result) {
	for (int i = 0; i < num_faces; i++) {
	    triangle_t t;
	    t.v[0] = faces[3*i+0];
	    t.v[1] = faces[3*i+1];
	    t.v[2] = faces[3*i+2];

	    ON_3dVector tdir = cmesh->tnorm(t);
	    ON_3dVector bdir = cmesh->bnorm(t);
	    bool flipped_tri = (ON_DotProduct(tdir, bdir) < 0);
	    if (flipped_tri) {
		t.v[2] = faces[3*i+1];
		t.v[1] = faces[3*i+2];
	    }

	    tris.insert(t);
	}

	bu_free(faces, "faces array");
    }

    bu_free(opoly, "polygon points");

    if (steiner) {
	bu_free(steiner, "faces array");
    }

    return result;
}

long
cpolygon_t::tri_process(std::set<edge_t> *ne, std::set<edge_t> *se, long *nv, triangle_t &t)
{
    std::set<cpolyedge_t *>::iterator pe_it;

    bool e_shared[3];
    struct edge_t e[3];
    struct uedge_t ue[3];
    for (int i = 0; i < 3; i++) {
	e_shared[i] = false;
    }
    e[0].set(t.v[0], t.v[1]);
    e[1].set(t.v[1], t.v[2]);
    e[2].set(t.v[2], t.v[0]);
    ue[0].set(t.v[0], t.v[1]);
    ue[1].set(t.v[1], t.v[2]);
    ue[2].set(t.v[2], t.v[0]);

    for (int i = 0; i < 3; i++) {
	for (pe_it = poly.begin(); pe_it != poly.end(); pe_it++) {
	    cpolyedge_t *pe = *pe_it;
	    struct uedge_t pue(pe->v[0], pe->v[1]);
	    if (ue[i] == pue) {
		e_shared[i] = true;
		break;
	    }
	}
    }

    long shared_cnt = 0;
    for (int i = 0; i < 3; i++) {
	if (e_shared[i]) {
	    shared_cnt++;
	    se->insert(e[i]);
	} else {
	    ne->insert(e[i]);
	}
    }

    if (shared_cnt == 0) {
	// If we don't have any shared edges any longer (we must have at the
	// start of processing or we wouldn't be here), we've probably got a
	// "bad" triangle that is already inside the loop due to another triangle
	// from the same shared edge expanding the loop.  Find the triangle
	// vertex that is the problem and mark it as an uncontained vertex.
	for (int i = 0; i < 3; i++) {
	    long vert = t.v[i];
	    if (cmesh->brep_edge_pnt(vert)) {
		continue;
	    }
	    int vert_problem_cnt = 0;
	    std::set<uedge_t>::iterator p_it;
	    for (p_it = problem_edges.begin(); p_it != problem_edges.end(); p_it++) {
		if (((*p_it).v[0] == vert) || ((*p_it).v[1] == vert)) {
		    vert_problem_cnt++;
		}
	    }

	    if (vert_problem_cnt > 1) {
		uncontained.insert(vert);
		(*nv) = -1;
		se->clear();
		ne->clear();
		return -2;
	    }
	}
    }

    if (shared_cnt == 1) {
	bool v_shared[3];
	for (int i = 0; i < 3; i++) {
	    v_shared[i] = false;
	}
	// If we've got only one shared edge, there should be a vertex not currently
	// involved with the loop - verify that, and if it's true report it.
	int vshared_cnt = 0;
	for (int i = 0; i < 3; i++) {
	    for (pe_it = poly.begin(); pe_it != poly.end(); pe_it++) {
		cpolyedge_t *pe = *pe_it;
		if (pe->v[0] == t.v[i] || pe->v[1] == t.v[i]) {
		    v_shared[i] = true;
		    vshared_cnt++;
		    break;
		}
	    }
	}
	if (vshared_cnt == 2) {
	    for (int i = 0; i < 3; i++) {
		if (v_shared[i] == false) {
		    (*nv) = t.v[i];
		    break;
		}
	    }

	    // If the uninvolved point is associated with bad edges, we can't use
	    // any of this to build the loop - it gets added to the uncontained
	    // points set, and we move on.
	    int vert_problem_cnt = 0;
	    std::set<uedge_t>::iterator p_it;
	    for (p_it = problem_edges.begin(); p_it != problem_edges.end(); p_it++) {
		if (((*p_it).v[0] == *nv) || ((*p_it).v[1] == *nv)) {
		    vert_problem_cnt++;
		}
	    }

	    if (vert_problem_cnt > 1) {
		if (!cmesh->brep_edge_pnt(*nv)) {
		    uncontained.insert(*nv);
		}
		(*nv) = -1;
		se->clear();
		ne->clear();
		return -2;
	    }

	} else {
	    // Self intersecting
	    (*nv) = -1;
	    se->clear();
	    ne->clear();
	    return -1;
	}
    }

    if (shared_cnt == 2) {
	// We've got one vert shared by both of the shared edges - it's probably
	// about to become an interior point
	std::map<long, int> vcnt;
	std::set<edge_t>::iterator se_it;
	for (se_it = se->begin(); se_it != se->end(); se_it++) {
	    vcnt[(*se_it).v[0]]++;
	    vcnt[(*se_it).v[1]]++;
	}
	std::map<long, int>::iterator v_it;
	for (v_it = vcnt.begin(); v_it != vcnt.end(); v_it++) {
	    if (v_it->second == 2) {
		(*nv) = v_it->first;
		break;
	    }
	}
    }

    return 3 - shared_cnt;
}

void cpolygon_t::polygon_plot(const char *filename)
{
    FILE* plot_file = fopen(filename, "w");
    struct bu_color c = BU_COLOR_INIT_ZERO;
    bu_color_rand(&c, BU_COLOR_RANDOM_LIGHTENED);
    pl_color_buc(plot_file, &c);

    point2d_t pmin, pmax;
    V2SET(pmin, DBL_MAX, DBL_MAX);
    V2SET(pmax, -DBL_MAX, -DBL_MAX);

    cpolyedge_t *efirst = *(poly.begin());
    cpolyedge_t *ecurr = NULL;

    point_t bnp;
    VSET(bnp, pnts_2d[efirst->v[0]][X], pnts_2d[efirst->v[0]][Y], 0);
    pdv_3move(plot_file, bnp);
    VSET(bnp, pnts_2d[efirst->v[1]][X], pnts_2d[efirst->v[1]][Y], 0);
    pdv_3cont(plot_file, bnp);

    V2MINMAX(pmin, pmax, pnts_2d[efirst->v[0]]);
    V2MINMAX(pmin, pmax, pnts_2d[efirst->v[1]]);

    size_t ecnt = 1;
    while (ecurr != efirst && ecnt < poly.size()+1) {
        ecurr = (!ecurr) ? efirst->next : ecurr->next;
        VSET(bnp, pnts_2d[ecurr->v[1]][X], pnts_2d[ecurr->v[1]][Y], 0);
        pdv_3cont(plot_file, bnp);
        V2MINMAX(pmin, pmax, pnts_2d[efirst->v[1]]);
    	if (ecnt > poly.size()) {
	    break;
	}
    }

    // Plot interior and uncontained points as well
    double r = DIST_PT2_PT2(pmin, pmax) * 0.01;
    std::set<long>::iterator p_it;

    // Interior
    pl_color(plot_file, 0, 255, 0);
    for (p_it = interior_points.begin(); p_it != interior_points.end(); p_it++) {
        point_t origin;
        VSET(origin, pnts_2d[*p_it][X], pnts_2d[*p_it][Y], 0);
        pdv_3move(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]+r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]-r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]-r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]+r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
    }

    // Uncontained
    pl_color(plot_file, 255, 0, 0);
    for (p_it = uncontained.begin(); p_it != uncontained.end(); p_it++) {
        point_t origin;
        VSET(origin, pnts_2d[*p_it][X], pnts_2d[*p_it][Y], 0);
        pdv_3move(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]+r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]-r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]-r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
        VSET(bnp, pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]+r, 0);
        pdv_3cont(plot_file, bnp);
        pdv_3cont(plot_file, origin);
    }

    fclose(plot_file);
}

void cpolygon_t::polygon_plot_in_plane(const char *filename)
{
    FILE* plot_file = fopen(filename, "w");
    struct bu_color c = BU_COLOR_INIT_ZERO;
    bu_color_rand(&c, BU_COLOR_RANDOM_LIGHTENED);
    pl_color_buc(plot_file, &c);

    ON_3dPoint ppnt;
    point_t pmin, pmax;
    point_t bnp;
    VSET(pmin, DBL_MAX, DBL_MAX, DBL_MAX);
    VSET(pmax, -DBL_MAX, -DBL_MAX, -DBL_MAX);

    cpolyedge_t *efirst = *(poly.begin());
    cpolyedge_t *ecurr = NULL;

    ppnt = tplane.PointAt(pnts_2d[efirst->v[0]][X], pnts_2d[efirst->v[0]][Y]);
    VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
    pdv_3move(plot_file, bnp);
    VMINMAX(pmin, pmax, bnp);
    ppnt = tplane.PointAt(pnts_2d[efirst->v[1]][X], pnts_2d[efirst->v[1]][Y]);
    VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
    pdv_3cont(plot_file, bnp);
    VMINMAX(pmin, pmax, bnp);

    size_t ecnt = 1;
    while (ecurr != efirst && ecnt < poly.size()+1) {
	ecurr = (!ecurr) ? efirst->next : ecurr->next;
	ppnt = tplane.PointAt(pnts_2d[ecurr->v[1]][X], pnts_2d[ecurr->v[1]][Y]);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	VMINMAX(pmin, pmax, bnp);
       	if (ecnt > poly.size()) {
	    break;
	}
    }

    // Plot interior and uncontained points as well
    double r = DIST_PT_PT(pmin, pmax) * 0.01;
    std::set<long>::iterator p_it;

    // Interior
    pl_color(plot_file, 0, 255, 0);
    for (p_it = interior_points.begin(); p_it != interior_points.end(); p_it++) {
	point_t origin;
	ppnt = tplane.PointAt(pnts_2d[*p_it][X], pnts_2d[*p_it][Y]);
	VSET(origin, ppnt.x, ppnt.y, ppnt.z);
	pdv_3move(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]+r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]-r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]-r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]+r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
    }

    // Uncontained
    pl_color(plot_file, 255, 0, 0);
    for (p_it = uncontained.begin(); p_it != uncontained.end(); p_it++) {
	point_t origin;
	ppnt = tplane.PointAt(pnts_2d[*p_it][X], pnts_2d[*p_it][Y]);
	VSET(origin, ppnt.x, ppnt.y, ppnt.z);
	pdv_3move(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]+r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]+r, pnts_2d[*p_it][Y]-r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]-r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
	ppnt = tplane.PointAt(pnts_2d[*p_it][X]-r, pnts_2d[*p_it][Y]+r);
	VSET(bnp, ppnt.x, ppnt.y, ppnt.z);
	pdv_3cont(plot_file, bnp);
	pdv_3cont(plot_file, origin);
    }

    fclose(plot_file);
}


void cpolygon_t::print()
{

    size_t ecnt = 1;
    cpolyedge_t *pe = (*poly.begin());
    cpolyedge_t *first = pe;
    cpolyedge_t *next = pe->next;

    std::set<cpolyedge_t *> visited;
    visited.insert(first);

    std::cout << first->v[0];

    // Walk the loop - an infinite loop is not closed
    while (first != next) {
	ecnt++;
	std::cout << "->" << next->v[0];
	visited.insert(next);
	next = next->next;
	if (ecnt > poly.size()) {
	    std::cout << " ERROR infinite loop\n";
	    return;
	}
    }

    std::cout << "\n";

    if (visited.size() != poly.size()) {
	std::cout << "Missing edges:\n";
	std::set<cpolyedge_t *>::iterator p_it;
	for (p_it = poly.begin(); p_it != poly.end(); p_it++) {
	    if (visited.find(*p_it) == visited.end()) {
		std::cout << "  " << (*p_it)->v[0] << "->" << (*p_it)->v[1] << "\n";
	    }
	}
    }
}

bool
cpolygon_t::have_uncontained()
{
    std::set<long>::iterator u_it;
    if (!uncontained.size()) return false;

    std::set<long> mvpnts;

    if (!closed()) return true;

    for (u_it = uncontained.begin(); u_it != uncontained.end(); u_it++) {
	if (point_in_polygon(*u_it, false)) {
	    mvpnts.insert(*u_it);
	}
    }
    for (u_it = mvpnts.begin(); u_it != mvpnts.end(); u_it++) {
	uncontained.erase(*u_it);
	interior_points.insert(*u_it);
    }

    return (uncontained.size() > 0) ? true : false;
}


extern "C" {
    HIDDEN int
	ctriangle_cmp(const void *p1, const void *p2, void *UNUSED(arg))
	{
	    struct ctriangle_t *t1 = *(struct ctriangle_t **)p1;
	    struct ctriangle_t *t2 = *(struct ctriangle_t **)p2;
	    if (t1->isect_edge && !t2->isect_edge) return 1;
	    if (!t1->isect_edge && t2->isect_edge) return 0;
	    if (t1->uses_uncontained && !t2->uses_uncontained) return 1;
	    if (!t1->uses_uncontained && t2->uses_uncontained) return 0;
	    if (t1->contains_uncontained && !t2->contains_uncontained) return 1;
	    if (!t1->contains_uncontained && t2->contains_uncontained) return 0;
	    if (t1->angle_to_nearest_uncontained > t2->angle_to_nearest_uncontained) return 1;
	    if (t1->angle_to_nearest_uncontained < t2->angle_to_nearest_uncontained) return 0;
	    bool c1 = (t1->v[0] < t2->v[0]);
	    bool c1e = (t1->v[0] == t2->v[0]);
	    bool c2 = (t1->v[1] < t2->v[1]);
	    bool c2e = (t1->v[1] == t2->v[1]);
	    bool c3 = (t1->v[2] < t2->v[2]);
	    return (c1 || (c1e && c2) || (c1e && c2e && c3));
	}
}

std::vector<struct ctriangle_t>
csweep_t::polygon_tris(double angle, bool brep_norm)
{
    std::set<triangle_t> initial_set;

    std::set<cpolyedge_t *>::iterator p_it;
    for (p_it = polygon.poly.begin(); p_it != polygon.poly.end(); p_it++) {
	cpolyedge_t *pe = *p_it;
	struct uedge_t ue(pe->v[0], pe->v[1]);
	bool edge_isect = (polygon.self_isect_edges.find(ue) != polygon.self_isect_edges.end());
	std::set<triangle_t> petris = cmesh->uedges2tris[ue];
	std::set<triangle_t>::iterator t_it;
	for (t_it = petris.begin(); t_it != petris.end(); t_it++) {

	    if (visited_triangles.find(*t_it) != visited_triangles.end()) {
	       	continue;
	    }

	    // If the triangle is involved with a self intersecting edge in the
	    // polygon and we haven't already see it, we have to try incorporating it
	    if (edge_isect) {
		initial_set.insert(*t_it);
		continue;
	    }

	    ON_3dVector tn = (brep_norm) ? cmesh->bnorm(*t_it) : cmesh->tnorm(*t_it);
	    double dprd = ON_DotProduct(pdir, tn);
	    double dang = (NEAR_EQUAL(dprd, 1.0, ON_ZERO_TOLERANCE)) ? 0 : acos(dprd);
	    if (dang > angle) {
		continue;
	    }
	    initial_set.insert(*t_it);
	}
    }

    // Now that we have the triangles, characterize them.
    struct ctriangle_t **ctris = (struct ctriangle_t **)bu_calloc(initial_set.size()+1, sizeof(ctriangle_t *), "sortable ctris");
    std::set<triangle_t>::iterator f_it;
    int ctris_cnt = 0;
    for (f_it = initial_set.begin(); f_it != initial_set.end(); f_it++) {

	struct ctriangle_t *nct = (struct ctriangle_t *)bu_calloc(1, sizeof(ctriangle_t), "ctriangle");
	ctris[ctris_cnt] = nct;
	ctris_cnt++;

	triangle_t t = *f_it;
	nct->v[0] = t.v[0];
	nct->v[1] = t.v[1];
	nct->v[2] = t.v[2];
	struct edge_t e1(t.v[0], t.v[1]);
	struct edge_t e2(t.v[1], t.v[2]);
	struct edge_t e3(t.v[2], t.v[0]);
	struct uedge_t ue[3];
	ue[0].set(t.v[0], t.v[1]);
	ue[1].set(t.v[1], t.v[2]);
	ue[2].set(t.v[2], t.v[0]);

	nct->isect_edge = false;
	nct->uses_uncontained = false;
	nct->contains_uncontained = false;
	nct->angle_to_nearest_uncontained = DBL_MAX;

	for (int i = 0; i < 3; i++) {
	    if (polygon.self_isect_edges.find(ue[i]) != polygon.self_isect_edges.end()) {
		nct->isect_edge = true;
		unusable_triangles.erase(*f_it);
	    }
	    if (nct->isect_edge) break;
	}
	if (nct->isect_edge) continue;


	// If we're not on a self-intersecting edge, check for use of uncontained points
	for (int i = 0; i < 3; i++) {
	    if (polygon.uncontained.find((t).v[i]) != polygon.uncontained.end()) {
		nct->uses_uncontained = true;
		unusable_triangles.erase(*f_it);
	    }
	    if (nct->uses_uncontained) break;
	    if (polygon.flipped_face.find((t).v[i]) != polygon.flipped_face.end()) {
		nct->uses_uncontained = true;
		unusable_triangles.erase(*f_it);
	    }
	    if (nct->uses_uncontained) break;
	}
	if (nct->uses_uncontained) continue;

	// If we aren't directly using an uncontained point, see if one is inside
	// the triangle projection
	cpolygon_t tpoly;
	tpoly.pnts_2d = polygon.pnts_2d;
	tpoly.add_edge(e1);
	tpoly.add_edge(e2);
	tpoly.add_edge(e3);
	std::set<long>::iterator u_it;
	for (u_it = polygon.uncontained.begin(); u_it != polygon.uncontained.end(); u_it++) {
	    if (tpoly.point_in_polygon(*u_it, false)) {
		nct->contains_uncontained = true;
		unusable_triangles.erase(*f_it);
	    }
	}
	if (!nct->contains_uncontained) {
	    for (u_it = polygon.flipped_face.begin(); u_it != polygon.flipped_face.end(); u_it++) {
		if (tpoly.point_in_polygon(*u_it, false)) {
		    nct->contains_uncontained = true;
		    unusable_triangles.erase(*f_it);
		}
	    }
	}
	if (nct->contains_uncontained) continue;

	// If we aren't directly involved with an uncontained point and we only
	// share 1 edge with the polygon, see how much it points at one of the
	// points of interest (if any) definitely outside the current polygon
	std::set<cpolyedge_t *>::iterator pe_it;
	long shared_cnt = polygon.shared_edge_cnt(t);
	if (shared_cnt != 1) continue;
	double vangle = polygon.ucv_angle(t);
	if (vangle > 0 && nct->angle_to_nearest_uncontained > vangle) {
	    nct->angle_to_nearest_uncontained = vangle;
	    unusable_triangles.erase(*f_it);
	}
    }

    // Now that we have the characterized triangles, sort them.
    bu_sort(ctris, ctris_cnt, sizeof(struct ctriangle_t *), ctriangle_cmp, NULL);


    // Push the final sorted results into the vector, free the ctris entries and array
    std::vector<ctriangle_t> result;
    for (long i = 0; i < ctris_cnt; i++) {
	result.push_back(*ctris[i]);
    }
    for (long i = 0; i < ctris_cnt; i++) {
	bu_free(ctris[i], "ctri");
    }
    bu_free(ctris, "ctris array");
    return result;
}

bool
ctriangle_vect_cmp(std::vector<ctriangle_t> &v1, std::vector<ctriangle_t> &v2)
{
    if (v1.size() != v2.size()) return false;

    for (size_t i = 0; i < v1.size(); i++) {
	for (int j = 0; j < 3; j++) {
	    if (v1[i].v[j] != v2[i].v[j]) return false;
	}
    }

    return true;
}

long
csweep_t::grow_loop(double deg, bool stop_on_contained)
{
    double angle = deg * ON_PI/180.0;

    if (stop_on_contained && !polygon.uncontained.size()) {
	return 0;
    }

    if (deg < 0 || deg > 170) {
	return -1;
    }

    unusable_triangles.clear();

    // First step - collect all the unvisited triangles from the polyline edges.

    std::stack<ctriangle_t> ts;

    std::set<edge_t> flipped_edges;

    std::vector<ctriangle_t> ptris = polygon_tris(angle, stop_on_contained);

    if (!ptris.size() && stop_on_contained) {
	std::cout << "No triangles available??\n";
	return -1;
    }
    if (!ptris.size() && !stop_on_contained) {
	return 0;
    }


    for (size_t i = 0; i < ptris.size(); i++) {
	ts.push(ptris[i]);
    }

    while (!ts.empty()) {

	ctriangle_t cct = ts.top();
	ts.pop();
	triangle_t ct(cct.v[0], cct.v[1], cct.v[2]);

	//polygon.polygon_plot_in_plane("poly_3d.plot3");

	// A triangle will introduce at most one new point into the loop.  If
	// the triangle is bad, it will define uncontained interior points and
	// potentially (if it has unmated edges) won't grow the polygon at all.

	// The first thing to do is find out of the triangle shares one or two
	// edges with the loop.  (0 or 3 would indicate something Very Wrong...)
	std::set<edge_t> new_edges;
	std::set<edge_t> shared_edges;
	std::set<edge_t>::iterator ne_it;
	long vert = -1;
	long new_edge_cnt = polygon.tri_process(&new_edges, &shared_edges, &vert, ct);

	bool process = true;

	if (new_edge_cnt == -2) {
	    // Vert from bad edges - added to uncontained.  Start over with another triangle - we
	    // need to walk out until this point is swept up by the polygon.
	    visited_triangles.insert(ct);
	    process = false;
	}

	if (new_edge_cnt == -1) {
	    // If the triangle shares one edge but all three vertices are on the
	    // polygon, we can't use this triangle in this context - it would produce
	    // a self-intersecting polygon.
	    unusable_triangles.insert(ct);
	    process = false;
	}

	if (process) {

	    ON_3dVector tdir = cmesh->tnorm(ct);
	    ON_3dVector bdir = cmesh->bnorm(ct);
	    bool flipped_tri = (ON_DotProduct(tdir, bdir) < 0);

	    if (stop_on_contained && new_edge_cnt == 2 && flipped_tri) {
		// It is possible that a flipped face adding two new edges will
		// make a mess out of the polygon (i.e. make it self intersecting.)
		// Tag it so we know we can't trust point_in_polygon until we've grown
		// the vertex out of flipped_face (remove_edge will handle that.)
		if (cmesh->brep_edge_pnt(vert)) {
		    // We can't flag brep edge points as uncontained, so if we hit this case
		    // flag all the verts except the edge verts as flipped face problem cases.
		    for (int i = 0; i < 3; i++) {
			if (!cmesh->brep_edge_pnt(ct.v[i])) {
			    polygon.flipped_face.insert(ct.v[i]);
			}
		    }
		} else {
		    polygon.flipped_face.insert(vert);
		}
	    }

	    int use_tri = 1;
	    if (!(polygon.poly.size() == 3 && polygon.interior_points.size())) {
		if (stop_on_contained && new_edge_cnt == 2 && !flipped_tri) {
		    // If this is a good triangle and we're in repair mode, don't add it unless
		    // it uses or points in the direction of at least one uncontained point.
		    if (!cct.isect_edge && !cct.uses_uncontained && !cct.contains_uncontained &&
			    (cct.angle_to_nearest_uncontained > 2*ON_PI || cct.angle_to_nearest_uncontained < 0)) {
			use_tri = 0;
		    }
		}
	    }

	    if (new_edge_cnt <= 0 || new_edge_cnt > 2) {
		std::cerr << "fatal loop growth error!\n";
		polygon.polygon_plot_in_plane("fatal_loop_growth_poly_3d.plot3");
		cmesh->tris_set_plot(visited_triangles, "fatal_loop_growth_visited_tris.plot3");
		cmesh->tri_plot(ct, "fatal_loop_growth_bad_tri.plot3");
		return -1;
	    }

	    if (use_tri) {
		polygon.replace_edges(new_edges, shared_edges);
		visited_triangles.insert(ct);
	    }
	}

	bool h_uc = polygon.have_uncontained();

	if (stop_on_contained && !h_uc && polygon.poly.size() > 3) {
	    polygon.cdt();
	    cmesh->tris_set_plot(polygon.tris, "patch.plot3");
	    return (long)cmesh->tris.size();
	}

	if (ts.empty()) {
	    // That's all the triangles from this ring - if we haven't
	    // terminated yet, pull the next triangle set.

	    if (!stop_on_contained) {
		angle = 0.75 * angle;
	    }

	    // We queue these up in a specific order - we want any triangles
	    // actually using flipped or uncontained vertices to be at the top
	    // of the stack (i.e. the first ones tried.  polygon_tris is responsible
	    // for sorting in priority order.
	    std::vector<struct ctriangle_t> ntris = polygon_tris(angle, stop_on_contained);

	    if (ctriangle_vect_cmp(ptris, ntris)) {
		std::cout << "Error - new triangle set from polygon edge is the same as the previous triangle set.  Infinite loop, aborting\n";
		std::vector<struct ctriangle_t> infinite_loop_tris = polygon_tris(angle, stop_on_contained);
		polygon.polygon_plot("infinite_loop_poly_2d.plot3");
		polygon.polygon_plot_in_plane("infinite_loop_poly_3d.plot3");
		cmesh->ctris_vect_plot(infinite_loop_tris, "infinite_loop_tris.plot3");
		return -1;
	    }
	    ptris.clear();
	    ptris = ntris;

	    for (size_t i = 0; i < ntris.size(); i++) {
		ts.push(ntris[i]);
	    }

	    if (!stop_on_contained && ts.empty()) {
		// per the current angle criteria we've got everything, and we're
		// not concerned with contained points so this isn't an indication
		// of an error condition.  Generate triangles.
		polygon.cdt();
		cmesh->tris_set_plot(polygon.tris, "patch.plot3");
		return (long)cmesh->tris.size();
	    }

	}
    }

    std::cout << "Error - loop growth terminated but conditions for triangulation were never satisfied!\n";
    polygon.polygon_plot("failed_patch_poly_2d.plot3");
    polygon.polygon_plot_in_plane("failed_patch_poly_3d.plot3");
    return -1;
}

void
csweep_t::build_2d_pnts(ON_3dPoint &c, ON_3dVector &n)
{
    pdir = n;
    ON_Plane tri_plane(c, n);
    polygon.pnts_2d = (point2d_t *)bu_calloc(cmesh->pnts.size() + 1, sizeof(point2d_t), "2D points array");
    for (size_t i = 0; i < cmesh->pnts.size(); i++) {
	double u, v;
	ON_3dPoint op3d = (*cmesh->pnts[i]);
	tri_plane.ClosestPointTo(op3d, &u, &v);
	polygon.pnts_2d[i][X] = u;
	polygon.pnts_2d[i][Y] = v;
    }
    tplane = tri_plane;

    polygon.cmesh = cmesh;
    polygon.tplane = tplane;
}


bool
csweep_t::build_initial_loop(triangle_t &seed, bool repair)
{
    std::set<uedge_t>::iterator u_it;

    if (repair) {
	// None of the edges or vertices from any of the problem triangles can be
	// in a polygon edge.  By definition, the seed is a problem triangle.
	std::set<long> seed_verts;
	for (int i = 0; i < 3; i++) {
	    seed_verts.insert(seed.v[i]);
	    // The exception to interior categorization is Brep boundary points -
	    // they are never interior or uncontained
	    if (cmesh->brep_edge_pnt(seed.v[i])) {
		continue;
	    }
	    polygon.uncontained.insert(seed.v[i]);
	}

	// We need a initial valid polygon loop to grow.  Poll the neighbor faces - if one
	// of them is valid, it will be used to build an initial loop
	std::vector<triangle_t> faces = cmesh->face_neighbors(seed);
	for (size_t i = 0; i < faces.size(); i++) {
	    triangle_t t = faces[i];
	    if (cmesh->seed_tris.find(t) == cmesh->seed_tris.end()) {
		struct edge_t e1(t.v[0], t.v[1]);
		struct edge_t e2(t.v[1], t.v[2]);
		struct edge_t e3(t.v[2], t.v[0]);
		polygon.add_edge(e1);
		polygon.add_edge(e2);
		polygon.add_edge(e3);
		visited_triangles.insert(t);
		return polygon.closed();
	    }
	}

	// If we didn't find a valid mated edge triangle (urk?) try the vertices
	for (int i = 0; i < 3; i++) {
	    std::vector<triangle_t> vfaces = cmesh->vertex_face_neighbors(seed.v[i]);
	    for (size_t j = 0; j < vfaces.size(); j++) {
		triangle_t t = vfaces[j];
		if (cmesh->seed_tris.find(t) == cmesh->seed_tris.end()) {
		    struct edge_t e1(t.v[0], t.v[1]);
		    struct edge_t e2(t.v[1], t.v[2]);
		    struct edge_t e3(t.v[2], t.v[0]);
		    polygon.add_edge(e1);
		    polygon.add_edge(e2);
		    polygon.add_edge(e3);
		    visited_triangles.insert(t);
		    return polygon.closed();
		}
	    }
	}

	// NONE of the triangles in the neighborhood are valid?  We'll have to hope that
	// subsequent processing of other seeds will put a proper mesh in contact with
	// this face...
	return false;

    }

    // If we're not repairing, start with the seed itself
    struct edge_t e1(seed.v[0], seed.v[1]);
    struct edge_t e2(seed.v[1], seed.v[2]);
    struct edge_t e3(seed.v[2], seed.v[0]);
    polygon.add_edge(e1);
    polygon.add_edge(e2);
    polygon.add_edge(e3);
    visited_triangles.insert(seed);
    return polygon.closed();
}

void csweep_t::plot_uedge(struct uedge_t &ue, FILE* plot_file)
{
    point_t bnp1, bnp2;
    VSET(bnp1, polygon.pnts_2d[ue.v[0]][X], polygon.pnts_2d[ue.v[0]][Y], 0);
    VSET(bnp2, polygon.pnts_2d[ue.v[1]][X], polygon.pnts_2d[ue.v[1]][Y], 0);
    pdv_3move(plot_file, bnp1);
    pdv_3cont(plot_file, bnp2);
}


void csweep_t::plot_tri(const triangle_t &t, struct bu_color *buc, FILE *plot, int r, int g, int b)
{
    point_t p[3];
    point_t porig;
    point_t c = VINIT_ZERO;

    for (int i = 0; i < 3; i++) {
	VSET(p[i], polygon.pnts_2d[t.v[i]][X], polygon.pnts_2d[t.v[i]][Y], 0);
	c[X] += p[i][X];
	c[Y] += p[i][Y];
    }
    c[X] = c[X]/3.0;
    c[Y] = c[Y]/3.0;
    c[Z] = 0;

    for (size_t i = 0; i < 3; i++) {
	if (i == 0) {
	    VMOVE(porig, p[i]);
	    pdv_3move(plot, p[i]);
	}
	pdv_3cont(plot, p[i]);
    }
    pdv_3cont(plot, porig);

    /* fill in the "interior" using the rgb color*/
    pl_color(plot, r, g, b);
    for (size_t i = 0; i < 3; i++) {
	pdv_3move(plot, p[i]);
	pdv_3cont(plot, c);
    }


    /* Plot the triangle normal */
    pl_color(plot, 0, 255, 255);
    {
	ON_3dVector tn = cmesh->tnorm(t);
	vect_t tnt;
	VSET(tnt, tn.x, tn.y, tn.z);
	point_t npnt;
	VADD2(npnt, tnt, c);
	pdv_3move(plot, c);
	pdv_3cont(plot, npnt);
    }

    /* Plot the brep normal */
    pl_color(plot, 0, 100, 0);
    {
	ON_3dVector tn = cmesh->bnorm(t);
	tn = tn * 0.5;
	vect_t tnt;
	VSET(tnt, tn.x, tn.y, tn.z);
	point_t npnt;
	VADD2(npnt, tnt, c);
	pdv_3move(plot, c);
	pdv_3cont(plot, npnt);
    }

    /* restore previous color */
    pl_color_buc(plot, buc);
}

void csweep_t::face_neighbors_plot(const triangle_t &f, const char *filename)
{
    FILE* plot_file = fopen(filename, "w");

    struct bu_color c = BU_COLOR_INIT_ZERO;
    bu_color_rand(&c, BU_COLOR_RANDOM_LIGHTENED);
    pl_color_buc(plot_file, &c);

    // Origin triangle has red interior
    std::vector<triangle_t> faces = cmesh->face_neighbors(f);
    plot_tri(f, &c, plot_file, 255, 0, 0);

    // Neighbor triangles have blue interior
    for (size_t i = 0; i < faces.size(); i++) {
	triangle_t tri = faces[i];
	plot_tri(tri, &c, plot_file, 0, 0, 255);
    }

    fclose(plot_file);
}

void csweep_t::vertex_face_neighbors_plot(long vind, const char *filename)
{
    FILE* plot_file = fopen(filename, "w");

    struct bu_color c = BU_COLOR_INIT_ZERO;
    bu_color_rand(&c, BU_COLOR_RANDOM_LIGHTENED);
    pl_color_buc(plot_file, &c);

    std::vector<triangle_t> faces = cmesh->vertex_face_neighbors(vind);

    for (size_t i = 0; i < faces.size(); i++) {
	triangle_t tri = faces[i];
	plot_tri(tri, &c, plot_file, 0, 0, 255);
    }

    // Plot the vind point that is the source of the triangles
    pl_color(plot_file, 0, 255,0);
    point2d_t p;
    V2MOVE(p, polygon.pnts_2d[vind]);
    pd_point(plot_file, p[X], p[Y]);
    fclose(plot_file);
}

}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8