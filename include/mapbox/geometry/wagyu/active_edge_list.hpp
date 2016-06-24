#pragma once

#include <mapbox/geometry/wagyu/config.hpp>
#include <mapbox/geometry/wagyu/exceptions.hpp>
#include <mapbox/geometry/wagyu/local_minimum.hpp>
#include <mapbox/geometry/wagyu/ring.hpp>
#include <mapbox/geometry/wagyu/scanbeam.hpp>
#include <mapbox/geometry/wagyu/sorted_edge_list.hpp>
#include <mapbox/geometry/wagyu/util.hpp>

namespace mapbox {
namespace geometry {
namespace wagyu {

template <typename T>
inline bool e2_inserts_before_e1(edge<T> const& e1, edge<T> const& e2) {
    if (e2.curr.x == e1.curr.x) {
        if (e2.top.y > e1.top.y) {
            return e2.top.x < get_current_x(e1, e2.top.y);
        } else {
            return e1.top.x > get_current_x(e2, e1.top.y);
        }
    } else {
        return e2.curr.x < e1.curr.x;
    }
}

template <typename T>
void insert_edge_into_AEL(edge_list_itr<T> edge, edge_list<T>& bound, edge_list<T>& active_edges) {
    auto itr = active_edges.begin();
    while (itr != active_edges.end() && !e2_inserts_before_e1(*itr, *edge)) {
        ++itr;
    }
    active_edges.splice(itr, bound, edge);
}

template <typename T>
void insert_edge_into_AEL(edge_list_itr<T> edge,
                          edge_list_itr<T> itr,
                          edge_list<T>& bound,
                          edge_list<T>& active_edges) {
    while (itr != active_edges.end() && !e2_inserts_before_e1(*itr, *edge)) {
        ++itr;
    }
    active_edges.splice(itr, bound, edge);
}

template <typename T>
void update_edge_into_AEL(edge_ptr<T>& e, edge_ptr<T>& active_edges, scanbeam_list<T>& scanbeam) {
    if (!e->next_in_LML) {
        throw clipper_exception("UpdateEdgeIntoAEL: invalid call");
    }

    e->next_in_LML->index = e->index;
    edge_ptr<T> Aelprev = e->prev_in_AEL;
    edge_ptr<T> Aelnext = e->next_in_AEL;
    if (Aelprev) {
        Aelprev->next_in_AEL = e->next_in_LML;
    } else {
        active_edges = e->next_in_LML;
    }
    if (Aelnext) {
        Aelnext->prev_in_AEL = e->next_in_LML;
    }
    e->next_in_LML->side = e->side;
    e->next_in_LML->winding_delta = e->winding_delta;
    e->next_in_LML->winding_count = e->winding_count;
    e->next_in_LML->winding_count2 = e->winding_count2;
    e = e->next_in_LML;
    e->curr = e->bot;
    e->prev_in_AEL = Aelprev;
    e->next_in_AEL = Aelnext;
    if (!is_horizontal(*e)) {
        scanbeam.push(e->top.y);
    }
}

template <typename T>
void set_winding_count(edge_list_itr<T> const& edge_itr,
                       edge_ptr_list<T>& active_edge_list,
                       clip_type cliptype,
                       fill_type subject_fill_type,
                       fill_type clip_fill_type) {
    using value_type = T;

    bound<T>& bnd = edge_itr->bound;
    auto e = edge_list_rev_itr<T>(edge_itr) if (e == active_edge_list.rend()) {
        if (bnd.winding_delta == 0) {
            fill_type pft =
                (bnd.poly_type == polygon_type_subject) ? subject_fill_type : clip_fill_type;
            bnd.winding_count = (pft == fill_type_negative ? -1 : 1);
        } else {
            bnd.winding_count = bnd.winding_delta;
        }
        bnd.winding_count2 = 0;
        return;
    }

    // find the edge of the same polytype that immediately preceeds 'edge' in
    // AEL
    while (e != active_edge_list.rend() &&
           (e->bound->poly_type != bnd.poly_type || e->bound->winding_delta == 0)) {
        ++e;
    }
    if (e == active_edge_list.rend()) {
        if (bnd.winding_delta == 0) {
            fill_type pft =
                (bnd.poly_type == polygon_type_subject) ? subject_fill_type : clip_fill_type;
            bnd.winding_count = (pft == fill_type_negative ? -1 : 1);
        } else {
            bnd.winding_count = bnd.winding_delta;
        }
        bnd.winding_count2 = 0;
    } else if (bnd.winding_delta == 0 && cliptype != clip_type_union) {
        bnd.winding_count = 1;
        bnd.winding_count2 = e->bound->winding_count2;
    } else if (is_even_odd_fill_type(bnd, subject_fill_type, clip_fill_type)) {
        // EvenOdd filling ...
        if (bnd.winding_delta == 0) {
            // are we inside a subj polygon ...
            bool inside = true;
            auto e2 = e;
            while (e2 != active_edge_list.rend()) {
                if (e2->bound->poly_type == e->bound->poly_type && e2->bound->winding_delta != 0) {
                    inside = !inside;
                }
                ++e2;
            }
            bnd.winding_count = (inside ? 0 : 1);
        } else {
            bnd.winding_count = bnd.winding_delta;
        }
        bnd.winding_count2 = e->bound->winding_count2;
    } else {
        // nonZero, Positive or Negative filling ...
        if (e->bound->winding_count * e->bound->winding_delta < 0) {
            // prev edge is 'decreasing' WindCount (WC) toward zero
            // so we're outside the previous polygon ...
            if (std::abs(e->bound->winding_count) > 1) {
                // outside prev poly but still inside another.
                // when reversing direction of prev poly use the same WC
                if (e->bound->winding_delta * bnd.winding_delta < 0) {
                    bnd.winding_count = e->bound->winding_count;
                } else {
                    // otherwise continue to 'decrease' WC ...
                    bnd.winding_count = e->bound->winding_count + bnd.winding_delta;
                }
            } else {
                // now outside all polys of same polytype so set own WC ...
                bnd.winding_count = (bnd.winding_delta == 0 ? 1 : bnd.winding_delta);
            }
        } else {
            // prev edge is 'increasing' WindCount (WC) away from zero
            // so we're inside the previous polygon ...
            if (bnd.winding_delta == 0) {
                bnd.winding_count = (e->bound->winding_count < 0 ? e->bound->winding_count - 1
                                                                 : e->bound->winding_count + 1);
            } else if (e->bound->winding_delta * bnd.winding_delta < 0) {
                // if wind direction is reversing prev then use same WC
                bnd.winding_count = e->bound->winding_count;
            } else {
                // otherwise add to WC ...
                bnd.winding_count = e->bound->winding_count + bnd.winding_delta;
            }
        }
        bnd.winding_count2 = e->bound->winding_count2;
    }

    // update winding_count2 ...
    auto e_foward = e.base();
    if (is_even_odd_alt_fill_type(bnd, subject_fill_type, clip_fill_type)) {
        // EvenOdd filling ...
        while (e_forward != edge_itr) {
            if (e_forward->bound->winding_delta != 0) {
                bnd.winding_count2 = (bnd.winding_count2 == 0 ? 1 : 0);
            }
            ++e_forward;
        }
    } else {
        // nonZero, Positive or Negative filling ...
        while (e_forward != edge_itr) {
            bnd.winding_count2 += e_forward->bound->winding_delta;
            ++e_forward;
        }
    }
}

template <typename T>
bool is_contributing(bound<T> const& bnd,
                     clip_type cliptype,
                     fill_type subject_fill_type,
                     fill_type clip_fill_type) {
    fill_type pft = subject_fill_type;
    fill_type pft2 = clip_fill_type;
    if (bnd.poly_type != polygon_type_subject) {
        pft = clip_fill_type;
        pft2 = subject_fill_type;
    }

    switch (pft) {
    case fill_type_even_odd:
        // return false if a subj line has been flagged as inside a subj
        // polygon
        if (bnd.winding_delta == 0 && bnd.winding_count != 1) {
            return false;
        }
        break;
    case fill_type_non_zero:
        if (std::abs(bnd.winding_count) != 1) {
            return false;
        }
        break;
    case fill_type_positive:
        if (bnd.winding_count != 1) {
            return false;
        }
        break;
    case fill_type_negative:
    default:
        if (bnd.winding_count != -1) {
            return false;
        }
    }

    switch (cliptype) {
    case clip_type_intersection:
        switch (pft2) {
        case fill_type_even_odd:
        case fill_type_non_zero:
            return (bnd.winding_count2 != 0);
        case fill_type_positive:
            return (bnd.winding_count2 > 0);
        case fill_type_negative:
        default:
            return (bnd.winding_count2 < 0);
        }
        break;
    case clip_type_union:
        switch (pft2) {
        case fill_type_even_odd:
        case fill_type_non_zero:
            return (bnd.winding_count2 == 0);
        case fill_type_positive:
            return (bnd.winding_count2 <= 0);
        case fill_type_negative:
        default:
            return (bnd.winding_count2 >= 0);
        }
        break;
    case clip_type_difference:
        if (bnd.poly_type == polygon_type_subject) {
            switch (pft2) {
            case fill_type_even_odd:
            case fill_type_non_zero:
                return (bnd.winding_count2 == 0);
            case fill_type_positive:
                return (bnd.winding_count2 <= 0);
            case fill_type_negative:
            default:
                return (bnd.winding_count2 >= 0);
            }
        } else {
            switch (pft2) {
            case fill_type_even_odd:
            case fill_type_non_zero:
                return (bnd.winding_count2 != 0);
            case fill_type_positive:
                return (bnd.winding_count2 > 0);
            case fill_type_negative:
            default:
                return (bnd.winding_count2 < 0);
            }
        }
        break;
    case clip_type_x_or:
        if (bnd.winding_delta == 0) {
            // XOr always contributing unless open
            switch (pft2) {
            case fill_type_even_odd:
            case fill_type_non_zero:
                return (bnd.winding_count2 == 0);
            case fill_type_positive:
                return (bnd.winding_count2 <= 0);
            case fill_type_negative:
            default:
                return (bnd.winding_count2 >= 0);
            }
        } else {
            return true;
        }
        break;
    default:
        return true;
    }
}

template <typename T>
edge_ptr<T> get_next_in_AEL(edge_ptr<T> e, horizontal_direction dir) {
    return dir == left_to_right ? e->next_in_AEL : e->prev_in_AEL;
}
}
}
}
