#include "xg.hpp"
#include "stream.hpp"

namespace xg {

id_t side_id(const side_t& side) {
    return abs(side);
}

bool side_is_end(const side_t& side) {
    return side < 0;
}

side_t make_side(id_t id, bool is_end) {
    return !is_end ? id : -1 * id;
}

id_t trav_id(const trav_t& trav) {
    return abs(trav.first);
}

bool trav_is_rev(const trav_t& trav) {
    return trav.first < 0;
}

int32_t trav_rank(const trav_t& trav) {
    return trav.second;
}

trav_t make_trav(id_t id, bool is_rev, int32_t rank) {
    return make_pair(!is_rev ? id : -1 * id, rank);
}

int dna3bit(char c) {
    switch (c) {
    case 'A':
        return 0;
    case 'T':
        return 1;
    case 'C':
        return 2;
    case 'G':
        return 3;
    default:
        return 4;
    }
}

char revdna3bit(int i) {
    switch (i) {
    case 0:
        return 'A';
    case 1:
        return 'T';
    case 2:
        return 'C';
    case 3:
        return 'G';
    default:
        return 'N';
    }
}

const XG::destination_t XG::BS_SEPARATOR = 1;
const XG::destination_t XG::BS_NULL = 0;

XG::XG(istream& in)
    : start_marker('#'),
      end_marker('$'),
      seq_length(0),
      node_count(0),
      edge_count(0),
      path_count(0) {
    load(in);
}

XG::XG(Graph& graph)
    : start_marker('#'),
      end_marker('$'),
      seq_length(0),
      node_count(0),
      edge_count(0),
      path_count(0) {
    from_graph(graph);
}

XG::XG(function<void(function<void(Graph&)>)> get_chunks)
    : start_marker('#'),
      end_marker('$'),
      seq_length(0),
      node_count(0),
      edge_count(0),
      path_count(0) {
    from_callback(get_chunks);
}

XG::~XG(void) {
}

void XG::load(istream& in) {

    if (!in.good()) {
        cerr << "[xg] error: index does not exist!" << endl;
        exit(1);
    }

    sdsl::read_member(seq_length, in);
    sdsl::read_member(node_count, in);
    sdsl::read_member(edge_count, in);
    sdsl::read_member(path_count, in);
    size_t entity_count = node_count + edge_count;
    //cerr << sequence_length << ", " << node_count << ", " << edge_count << endl;
    sdsl::read_member(min_id, in);
    sdsl::read_member(max_id, in);

    i_iv.load(in);
    r_iv.load(in);

    s_iv.load(in);
    s_cbv.load(in);
    s_cbv_rank.load(in, &s_cbv);
    s_cbv_select.load(in, &s_cbv);

    f_iv.load(in);
    f_bv.load(in);
    f_bv_rank.load(in, &f_bv);
    f_bv_select.load(in, &f_bv);
    f_from_start_cbv.load(in);
    f_to_end_cbv.load(in);

    t_iv.load(in);
    t_bv.load(in);
    t_bv_rank.load(in, &t_bv);
    t_bv_select.load(in, &t_bv);
    t_to_end_cbv.load(in);
    t_from_start_cbv.load(in);

    pn_iv.load(in);
    pn_csa.load(in);
    pn_bv.load(in);
    pn_bv_rank.load(in, &pn_bv);
    pn_bv_select.load(in, &pn_bv);
    pi_iv.load(in);
    sdsl::read_member(path_count, in);
    for (size_t i = 0; i < path_count; ++i) {
        auto path = new XGPath;
        path->load(in);
        paths.push_back(path);
    }
    ep_iv.load(in);
    ep_bv.load(in);
    ep_bv_rank.load(in, &ep_bv);
    ep_bv_select.load(in, &ep_bv);

    h_iv.load(in);
    ts_iv.load(in);

    // Load all the B_s arrays for sides.
    // Baking required before serialization.
    deserialize(bs_single_array, in);
}

void XGPath::load(istream& in) {
    members.load(in);
    members_rank.load(in, &members);
    members_select.load(in, &members);
    ids.load(in);
    directions.load(in);
    ranks.load(in);
    positions.load(in);
    offsets.load(in);
    offsets_rank.load(in, &offsets);
    offsets_select.load(in, &offsets);
}

size_t XGPath::serialize(std::ostream& out,
                         sdsl::structure_tree_node* v,
                         std::string name) {
    sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
    size_t written = 0;
    written += members.serialize(out, child, "path_membership_" + name);
    written += ids.serialize(out, child, "path_node_ids_" + name);
    written += directions.serialize(out, child, "path_node_directions_" + name);
    written += ranks.serialize(out, child, "path_mapping_ranks_" + name);
    written += positions.serialize(out, child, "path_node_offsets_" + name);
    written += offsets.serialize(out, child, "path_node_starts_" + name);
    written += offsets_rank.serialize(out, child, "path_node_starts_rank_" + name);
    written += offsets_select.serialize(out, child, "path_node_starts_select_" + name);

    sdsl::structure_tree::add_size(child, written);

    return written;
}

XGPath::XGPath(const string& path_name,
               const vector<trav_t>& path,
               size_t entity_count,
               XG& graph,
               size_t* unique_member_count_out) {

    // path members (of nodes and edges ordered as per f_bv)
    bit_vector members_bv;
    util::assign(members_bv, bit_vector(entity_count));
    // node ids, the literal path
    int_vector<> ids_iv;
    util::assign(ids_iv, int_vector<>(path.size()));
    // directions of traversal (typically forward, but we allow backwards
    bit_vector directions_bv;
    util::assign(directions_bv, bit_vector(path.size()));
    // node positions in path
    util::assign(positions, int_vector<>(path.size()));
    // mapping ranks in path
    util::assign(ranks, int_vector<>(path.size()));

    size_t path_off = 0;
    size_t members_off = 0;
    size_t positions_off = 0;
    size_t path_length = 0;

    // determine total length
    for (size_t i = 0; i < path.size(); ++i) {
        auto node_id = trav_id(path[i]);
        path_length += graph.node_length(node_id);
        ids_iv[i] = node_id;
        // we will explode if the node isn't in the graph
    }

    // make the bitvector for path offsets
    util::assign(offsets, bit_vector(path_length));
    set<int64_t> uniq_nodes;
    set<pair<pair<int64_t, bool>, pair<int64_t, bool>>> uniq_edges;
    //cerr << "path " << path_name << " has " << path.size() << endl;
    for (size_t i = 0; i < path.size(); ++i) {
        //cerr << i << endl;
        auto& trav = path[i];
        auto node_id = trav_id(trav);
        bool is_reverse = trav_is_rev(trav);
        //cerr << node_id << endl;
        // record node
        members_bv[graph.node_rank_as_entity(node_id)-1] = 1;
        // record direction of passage through node
        directions_bv[i] = is_reverse;
        // and the external rank of the mapping
        ranks[i] = trav_rank(trav);
        // we've seen another entity
        uniq_nodes.insert(node_id);
        // and record node offset in path
        positions[positions_off++] = path_off;
        // record position of node
        offsets[path_off] = 1;
        // and update the offset counter
        path_off += graph.node_length(node_id);

        // find the next edge in the path, and record it
        if (i+1 < path.size()) { // but only if there is a next node
            auto next_node_id = trav_id(path[i+1]);
            bool next_is_reverse = trav_is_rev(path[i+1]);
            //cerr << "checking if we have the edge" << endl;
            int64_t id1, id2;
            bool rev1, rev2;
            if (is_reverse && next_is_reverse) {
                id1 = next_node_id; id2 = node_id;
                rev1 = false; rev2 = false;
            } else {
                id1 = node_id; id2 = next_node_id;
                rev1 = is_reverse; rev2 = next_is_reverse;
            }
            if (graph.has_edge(id1, rev1, id2, rev2)) {
                members_bv[graph.edge_rank_as_entity(id1, rev1, id2, rev2)-1] = 1;
                uniq_edges.insert(
                    make_pair(
                        make_pair(id1, rev1),
                        make_pair(id2, rev2)));
            } else if (graph.has_edge(id2, !rev2, id1, !rev1)) {
                members_bv[graph.edge_rank_as_entity(id2, !rev2, id1, !rev1)-1] = 1;
                uniq_edges.insert(
                    make_pair(
                        make_pair(id2, !rev2),
                        make_pair(id1, !rev1)
                        ));
            } else {
                cerr << "[xg] warning: graph does not have edge from "
                     << node_id << (trav_is_rev(path[i])?"+":"-")
                     << " to "
                     << next_node_id << (trav_is_rev(path[i+1])?"-":"+")
                     << " for path " << path_name
                     << endl;
            }
        }
    }
    //cerr << uniq_nodes.size() << " vs " << path.size() << endl;
    if(unique_member_count_out) {
        // set member count as the unique entities that are in the path
        // We don't need it but our caller might
        *unique_member_count_out = uniq_nodes.size() + uniq_edges.size();
    }
    // compress path membership vectors
    util::assign(members, sd_vector<>(members_bv));
    // and traversal information
    util::assign(directions, sd_vector<>(directions_bv));
    // handle entity lookup structure (wavelet tree)
    util::bit_compress(ids_iv);
    construct_im(ids, ids_iv);
    // bit compress the positional offset info
    util::bit_compress(positions);
    // bit compress mapping ranks
    util::bit_compress(ranks);

    util::assign(offsets_rank, rank_support_v<1>(&offsets));
    util::assign(offsets_select, bit_vector::select_1_type(&offsets));
}

Mapping XGPath::mapping(size_t offset) {
    // TODO actually store the "real" mapping
    Mapping m;
    // store the starting position and series of edits
    m.mutable_position()->set_node_id(ids[offset]);
    m.mutable_position()->set_is_reverse(directions[offset]);
    m.set_rank(ranks[offset]);
    return m;
}

size_t XG::serialize(ostream& out, sdsl::structure_tree_node* s, std::string name) {

    sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(s, name, sdsl::util::class_name(*this));
    size_t written = 0;

    written += sdsl::write_member(s_iv.size(), out, child, "sequence_length");
    written += sdsl::write_member(i_iv.size(), out, child, "node_count");
    written += sdsl::write_member(f_iv.size()-i_iv.size(), out, child, "edge_count");
    written += sdsl::write_member(path_count, out, child, "path_count");
    written += sdsl::write_member(min_id, out, child, "min_id");
    written += sdsl::write_member(max_id, out, child, "max_id");

    written += i_iv.serialize(out, child, "id_rank_vector");
    written += r_iv.serialize(out, child, "rank_id_vector");

    written += s_iv.serialize(out, child, "seq_vector");
    written += s_cbv.serialize(out, child, "seq_node_starts");
    written += s_cbv_rank.serialize(out, child, "seq_node_starts_rank");
    written += s_cbv_select.serialize(out, child, "seq_node_starts_select");

    written += f_iv.serialize(out, child, "from_vector");
    written += f_bv.serialize(out, child, "from_node");
    written += f_bv_rank.serialize(out, child, "from_node_rank");
    written += f_bv_select.serialize(out, child, "from_node_select");
    written += f_from_start_cbv.serialize(out, child, "from_is_from_start");
    written += f_to_end_cbv.serialize(out, child, "from_is_to_end");

    written += t_iv.serialize(out, child, "to_vector");
    written += t_bv.serialize(out, child, "to_node");
    written += t_bv_rank.serialize(out, child, "to_node_rank");
    written += t_bv_select.serialize(out, child, "to_node_select");
    written += t_to_end_cbv.serialize(out, child, "to_is_to_end");
    written += t_from_start_cbv.serialize(out, child, "to_is_from_start");

    // Treat the paths as their own node
    size_t paths_written = 0;
    auto paths_child = sdsl::structure_tree::add_child(child, "paths", sdsl::util::class_name(*this));

    paths_written += pn_iv.serialize(out, paths_child, "path_names");
    paths_written += pn_csa.serialize(out, paths_child, "path_names_csa");
    paths_written += pn_bv.serialize(out, paths_child, "path_names_starts");
    paths_written += pn_bv_rank.serialize(out, paths_child, "path_names_starts_rank");
    paths_written += pn_bv_select.serialize(out, paths_child, "path_names_starts_select");
    paths_written += pi_iv.serialize(out, paths_child, "path_ids");
    paths_written += sdsl::write_member(paths.size(), out, paths_child, "path_count");
    for (size_t i = 0; i < paths.size(); i++) {
        XGPath* path = paths[i];
        paths_written += path->serialize(out, paths_child, "path:" + path_name(i + 1));
    }

    paths_written += ep_iv.serialize(out, paths_child, "entity_path_mapping");
    paths_written += ep_bv.serialize(out, paths_child, "entity_path_mapping_starts");
    paths_written += ep_bv_rank.serialize(out, paths_child, "entity_path_mapping_starts_rank");
    paths_written += ep_bv_select.serialize(out, paths_child, "entity_path_mapping_starts_select");

    sdsl::structure_tree::add_size(paths_child, paths_written);
    written += paths_written;

    // Treat the threads as their own node.
    // This will mess up any sort of average size stats, but it will also be useful.
    auto threads_child = sdsl::structure_tree::add_child(child, "threads", sdsl::util::class_name(*this));
    size_t threads_written = 0;
    threads_written += h_iv.serialize(out, threads_child, "thread_usage_count");
    threads_written += ts_iv.serialize(out, threads_child, "thread_start_count");
    // Stick all the B_s arrays in together. Must be baked.
    threads_written += xg::serialize(bs_single_array, out, threads_child, "bs_single_array");

    sdsl::structure_tree::add_size(threads_child, threads_written);
    written += threads_written;

    sdsl::structure_tree::add_size(child, written);
    return written;

}

void XG::from_stream(istream& in, bool validate_graph, bool print_graph,
    bool store_threads, bool is_sorted_dag) {

    from_callback([&](function<void(Graph&)> handle_chunk) {
        // TODO: should I be bandying about function references instead of
        // function objects here?
        stream::for_each(in, handle_chunk);
    }, validate_graph, print_graph, store_threads, is_sorted_dag);
}

void XG::from_graph(Graph& graph, bool validate_graph, bool print_graph,
    bool store_threads, bool is_sorted_dag) {

    from_callback([&](function<void(Graph&)> handle_chunk) {
        // There's only one chunk in this case.
        handle_chunk(graph);
    }, validate_graph, print_graph, store_threads, is_sorted_dag);

}

void XG::from_callback(function<void(function<void(Graph&)>)> get_chunks,
    bool validate_graph, bool print_graph, bool store_threads, bool is_sorted_dag) {

    // temporaries for construction
    map<id_t, string> node_label;
    // need to store node sides
    map<side_t, set<side_t> > from_to;
    map<side_t, set<side_t> > to_from;
    map<string, vector<trav_t> > path_nodes;

    // This takes in graph chunks and adds them into our temporary storage.
    function<void(Graph&)> lambda = [this,
                                     &node_label,
                                     &from_to,
                                     &to_from,
                                     &path_nodes](Graph& graph) {
        for (int i = 0; i < graph.node_size(); ++i) {
            const Node& n = graph.node(i);
            if (node_label.find(n.id()) == node_label.end()) {
                ++node_count;
                seq_length += n.sequence().size();
                node_label[n.id()] = n.sequence();
            }
        }
        for (int i = 0; i < graph.edge_size(); ++i) {
            const Edge& e = graph.edge(i);
            if (from_to.find(make_side(e.from(), e.from_start())) == from_to.end()
                || from_to[make_side(e.from(), e.from_start())].count(make_side(e.to(), e.to_end())) == 0) {
                ++edge_count;
                from_to[make_side(e.from(), e.from_start())].insert(make_side(e.to(), e.to_end()));
                to_from[make_side(e.to(), e.to_end())].insert(make_side(e.from(), e.from_start()));
            }
        }

        // Print out all the paths in the graph we are loading
        for (int i = 0; i < graph.path_size(); ++i) {
            const Path& p = graph.path(i);
            const string& name = p.name();
#ifdef VERBOSE_DEBUG
            cerr << "Path " << name << ": ";
#endif
            vector<trav_t>& path = path_nodes[name];
            for (int j = 0; j < p.mapping_size(); ++j) {
                const Mapping& m = p.mapping(j);
                path.push_back(make_trav(m.position().node_id(), m.position().is_reverse(), m.rank()));
#ifdef VERBOSE_DEBUG
                cerr << m.position().node_id() * 2 + m.position().is_reverse() << "; ";
#endif
            }
#ifdef VERBOSE_DEBUG
            cerr << endl;
#endif

        }
    };

    // Get all the chunks via the callback, and have them called back to us.
    // The other end handles figuring out how much to loop.
    get_chunks(lambda);

    path_count = path_nodes.size();

    // sort the paths using mapping rank
    // and remove duplicates
    for (auto& p : path_nodes) {
        vector<trav_t>& path = path_nodes[p.first];
        std::sort(path.begin(), path.end(),
                  [](const trav_t& m1, const trav_t& m2) { return trav_rank(m1) < trav_rank(m2); });
        path.erase(std::unique(path.begin(), path.end(),
                               [](const trav_t& m1, const trav_t& m2) {
                                   return trav_rank(m1) == trav_rank(m2);
                               }),
                   path.end());
    }

    build(node_label, from_to, to_from, path_nodes, validate_graph, print_graph,
        store_threads, is_sorted_dag);

}

void XG::build(map<id_t, string>& node_label,
               map<side_t, set<side_t> >& from_to,
               map<side_t, set<side_t> >& to_from,
               map<string, vector<trav_t> >& path_nodes,
               bool validate_graph,
               bool print_graph,
               bool store_threads,
               bool is_sorted_dag) {

    size_t entity_count = node_count + edge_count;
#ifdef VERBOSE_DEBUG
    cerr << "graph has " << seq_length << "bp in sequence, "
         << node_count << " nodes, "
         << edge_count << " edges, and "
         << path_count << " paths "
         << "for a total of " << entity_count << " entities" << endl;
#endif

    // for mapping of ids to ranks using a vector rather than wavelet tree
    min_id = node_label.begin()->first;
    max_id = node_label.rbegin()->first;

    // set up our compressed representation
    util::assign(s_iv, int_vector<>(seq_length, 0, 3));
    util::assign(s_bv, bit_vector(seq_length));
    util::assign(i_iv, int_vector<>(node_count));
    util::assign(r_iv, int_vector<>(max_id-min_id+1)); // note possibly discontiguous
    util::assign(f_iv, int_vector<>(entity_count));
    util::assign(f_bv, bit_vector(entity_count));
    util::assign(f_from_start_bv, bit_vector(entity_count));
    util::assign(f_to_end_bv, bit_vector(entity_count));
    util::assign(t_iv, int_vector<>(entity_count));
    util::assign(t_bv, bit_vector(entity_count));
    util::assign(t_to_end_bv, bit_vector(entity_count));
    util::assign(t_from_start_bv, bit_vector(entity_count));

    // for each node in the sequence
    // concatenate the labels into the s_iv
#ifdef VERBOSE_DEBUG
    cerr << "storing node labels" << endl;
#endif
    size_t i = 0; // insertion point
    size_t r = 1;
    for (auto& p : node_label) {
        int64_t id = p.first;
        const string& l = p.second;
        s_bv[i] = 1; // record node start
        i_iv[r-1] = id;
        // store ids to rank mapping
        r_iv[id-min_id] = r;
        ++r;
        for (auto c : l) {
            s_iv[i++] = dna3bit(c); // store sequence
        }
    }
    // keep only if we need to validate the graph
    if (!validate_graph) node_label.clear();

    // we have to process all the nodes before we do the edges
    // because we need to ensure full coverage of node space

    util::bit_compress(i_iv);
    util::bit_compress(r_iv);

#ifdef VERBOSE_DEBUG
    cerr << "storing forward edges and adjacency table" << endl;
#endif
    size_t f_itr = 0;
    size_t j_itr = 0; // edge adjacency pointer
    for (size_t k = 0; k < node_count; ++k) {
        int64_t f_id = i_iv[k];
        size_t f_rank = k+1;
        f_iv[f_itr] = f_rank;
        f_bv[f_itr] = 1;
        ++f_itr;
        for (auto end : { false, true }) {
            if (from_to.find(make_side(f_id, end)) != from_to.end()) {
                auto t_side_itr = from_to.find(make_side(f_id, end));
                if (t_side_itr != from_to.end()) {
                    for (auto& t_side : t_side_itr->second) {
                        size_t t_rank = id_to_rank(side_id(t_side));
                        // store link
                        f_iv[f_itr] = t_rank;
                        f_bv[f_itr] = 0;
                        // store side for start of edge
                        f_from_start_bv[f_itr] = end;
                        f_to_end_bv[f_itr] = side_is_end(t_side);
                        ++f_itr;
                    }
                }
            }
        }
    }

    // compress the forward direction side information
    util::assign(f_from_start_cbv, sd_vector<>(f_from_start_bv));
    util::assign(f_to_end_cbv, sd_vector<>(f_to_end_bv));

    //assert(e_iv.size() == edge_count*3);
#ifdef VERBOSE_DEBUG
    cerr << "storing reverse edges" << endl;
#endif

    size_t t_itr = 0;
    for (size_t k = 0; k < node_count; ++k) {
        //cerr << k << endl;
        int64_t t_id = i_iv[k];
        size_t t_rank = k+1;
        t_iv[t_itr] = t_rank;
        t_bv[t_itr] = 1;
        ++t_itr;
        for (auto end : { false, true }) {
            if (to_from.find(make_side(t_id, end)) != to_from.end()) {
                auto f_side_itr = to_from.find(make_side(t_id, end));
                if (f_side_itr != to_from.end()) {
                    for (auto& f_side : f_side_itr->second) {
                        size_t f_rank = id_to_rank(side_id(f_side));
                        // store link
                        t_iv[t_itr] = f_rank;
                        t_bv[t_itr] = 0;
                        // store side for end of edge
                        t_to_end_bv[t_itr] = end;
                        t_from_start_bv[t_itr] = side_is_end(f_side);
                        ++t_itr;
                    }
                }
            }
        }
    }

    // compress the reverse direction side information
    util::assign(t_to_end_cbv, sd_vector<>(t_to_end_bv));
    util::assign(t_from_start_cbv, sd_vector<>(t_from_start_bv));


    /*
    csa_wt<wt_int<rrr_vector<63>>> csa;
    int_vector<> x = {1,8,15,23,1,8,23,11,8};
    construct_im(csa, x, 8);
    cout << " i SA ISA PSI LF BWT   T[SA[i]..SA[i]-1]" << endl;
    csXprintf(cout, "%2I %2S %3s %3P %2p %3B   %:3T", csa);
    */
    /*
    cerr << "building csa of edges" << endl;
    //string edges_file = "@edges.iv";
    //store_to_file(e_iv, edges_file);
    construct_im(e_csa, e_iv, 1);
    */

    // to label the paths we'll need to compress and index our vectors
    util::bit_compress(s_iv);
    util::bit_compress(f_iv);
    util::bit_compress(t_iv);
    //util::bit_compress(e_iv);

    //construct_im(e_csa, e_iv, 8);

    util::assign(s_bv_rank, rank_support_v<1>(&s_bv));
    util::assign(s_bv_select, bit_vector::select_1_type(&s_bv));
    util::assign(f_bv_rank, rank_support_v<1>(&f_bv));
    util::assign(f_bv_select, bit_vector::select_1_type(&f_bv));
    util::assign(t_bv_rank, rank_support_v<1>(&t_bv));
    util::assign(t_bv_select, bit_vector::select_1_type(&t_bv));

    // compressed vectors of the above
    //vlc_vector<> s_civ(s_iv);
    util::assign(s_cbv, rrr_vector<>(s_bv));
    util::assign(s_cbv_rank, rrr_vector<>::rank_1_type(&s_cbv));
    util::assign(s_cbv_select, rrr_vector<>::select_1_type(&s_cbv));

// Prepare empty vectors for path indexing
#ifdef VERBOSE_DEBUG
    cerr << "creating empty succinct thread store" << endl;
#endif
    util::assign(h_iv, int_vector<>(entity_count * 2, 0));
    util::assign(ts_iv, int_vector<>((node_count + 1) * 2, 0));

#if GPBWT_MODE == MODE_SDSL
    // We have one B_s array for every side, but the first 2 numbers for sides
    // are unused. But max node rank is inclusive, so it evens out...
    bs_arrays.resize(max_node_rank() * 2);
#endif

#ifdef VERBOSE_DEBUG
    cerr << "storing paths" << endl;
#endif
    // paths
    //path_nodes[name].push_back(m.position().node_id());
    string path_names;
    size_t path_entities = 0; // count of nodes and edges
    for (auto& pathpair : path_nodes) {
        // add path name
        const string& path_name = pathpair.first;
        //cerr << path_name << endl;
        path_names += start_marker + path_name + end_marker;
        // The path constructor helpfully counts unique path members for us
        size_t unique_member_count;
        XGPath* path = new XGPath(path_name, pathpair.second, entity_count, *this, &unique_member_count);
        paths.push_back(path);
        path_entities += unique_member_count;
    }

    // handle path names
    util::assign(pn_iv, int_vector<>(path_names.size()));
    util::assign(pn_bv, bit_vector(path_names.size()));
    // now record path name starts
    for (size_t i = 0; i < path_names.size(); ++i) {
        pn_iv[i] = path_names[i];
        if (path_names[i] == start_marker) {
            pn_bv[i] = 1; // register name start
        }
    }
    util::assign(pn_bv_rank, rank_support_v<1>(&pn_bv));
    util::assign(pn_bv_select, bit_vector::select_1_type(&pn_bv));

    //util::bit_compress(pn_iv);
    string path_name_file = "@pathnames.iv";
    store_to_file((const char*)path_names.c_str(), path_name_file);
    construct(pn_csa, path_name_file, 1);

    // entity -> paths
    util::assign(ep_iv, int_vector<>(path_entities+entity_count));
    util::assign(ep_bv, bit_vector(path_entities+entity_count));
    size_t ep_off = 0;
    for (size_t i = 0; i < entity_count; ++i) {
        ep_bv[ep_off] = 1;
        ep_iv[ep_off] = 0; // null so we can detect entities with no path membership
        ++ep_off;
        for (size_t j = 0; j < paths.size(); ++j) {
            if (paths[j]->members[i] == 1) {
                ep_iv[ep_off++] = j+1;
            }
        }
    }

    util::bit_compress(ep_iv);
    //cerr << ep_off << " " << path_entities << " " << entity_count << endl;
    assert(ep_off <= path_entities+entity_count);
    util::assign(ep_bv_rank, rank_support_v<1>(&ep_bv));
    util::assign(ep_bv_select, bit_vector::select_1_type(&ep_bv));

    if(store_threads) {

#ifdef VERBOSE_DEBUG
        cerr << "storing threads" << endl;
#endif

        // If we're a sorted DAG we'll batch up the paths and use a batch
        // insert.
        vector<thread_t> batch;

        // Just store all the paths that are all perfect mappings as threads.
        // We end up converting *back* into thread_t objects.
        for (auto& pathpair : path_nodes) {
            thread_t reconstructed;

            // Grab the trav_ts, which are now sorted by rank
            for (auto& m : pathpair.second) {
                // Convert the mapping to a ThreadMapping
                // trav_ts are already rank sorted and deduplicated.
                ThreadMapping mapping = {trav_id(m), trav_is_rev(m)};
                reconstructed.push_back(mapping);
            }

#if GPBWT_MODE == MODE_SDSL
            if(is_sorted_dag) {
                // Save for a batch insert
                batch.push_back(reconstructed);
            }
            // TODO: else case!
#elif GPBWT_MODE == MODE_DYNAMIC
            // Insert the thread right now
            insert_thread(reconstructed);
#endif

        }

#if GPBWT_MODE == MODE_SDSL
        if(is_sorted_dag) {
            // Do the batch insert
            insert_threads_into_dag(batch);
        }
        // TODO: else case!
#endif
    }


#ifdef DEBUG_CONSTRUCTION
    cerr << "|s_iv| = " << size_in_mega_bytes(s_iv) << endl;
    cerr << "|f_iv| = " << size_in_mega_bytes(f_iv) << endl;
    cerr << "|t_iv| = " << size_in_mega_bytes(t_iv) << endl;

    cerr << "|f_from_start_cbv| = " << size_in_mega_bytes(f_from_start_cbv) << endl;
    cerr << "|t_to_end_cbv| = " << size_in_mega_bytes(t_to_end_cbv) << endl;

    cerr << "|f_bv| = " << size_in_mega_bytes(f_bv) << endl;
    cerr << "|t_bv| = " << size_in_mega_bytes(t_bv) << endl;

    cerr << "|i_iv| = " << size_in_mega_bytes(i_iv) << endl;
    //cerr << "|i_wt| = " << size_in_mega_bytes(i_wt) << endl;

    cerr << "|s_cbv| = " << size_in_mega_bytes(s_cbv) << endl;

    cerr << "|h_iv| = " << size_in_mega_bytes(h_iv) << endl;
    cerr << "|ts_iv| = " << size_in_mega_bytes(ts_iv) << endl;

    long double paths_mb_size = 0;
    cerr << "|pn_iv| = " << size_in_mega_bytes(pn_iv) << endl;
    paths_mb_size += size_in_mega_bytes(pn_iv);
    cerr << "|pn_csa| = " << size_in_mega_bytes(pn_csa) << endl;
    paths_mb_size += size_in_mega_bytes(pn_csa);
    cerr << "|pn_bv| = " << size_in_mega_bytes(pn_bv) << endl;
    paths_mb_size += size_in_mega_bytes(pn_bv);
    paths_mb_size += size_in_mega_bytes(pn_bv_rank);
    paths_mb_size += size_in_mega_bytes(pn_bv_select);
    paths_mb_size += size_in_mega_bytes(pi_iv);
    cerr << "|ep_iv| = " << size_in_mega_bytes(ep_iv) << endl;
    paths_mb_size += size_in_mega_bytes(ep_iv);
    cerr << "|ep_bv| = " << size_in_mega_bytes(ep_bv) << endl;
    paths_mb_size += size_in_mega_bytes(ep_bv);
    paths_mb_size += size_in_mega_bytes(ep_bv_rank);
    paths_mb_size += size_in_mega_bytes(ep_bv_select);
    cerr << "total paths size " << paths_mb_size << endl;
    // TODO you are missing the rest of the paths size in xg::paths
    // but this fragment should be factored into a function anyway

    cerr << "total size [MB] = " << (
        size_in_mega_bytes(s_iv)
        + size_in_mega_bytes(f_iv)
        + size_in_mega_bytes(t_iv)
        //+ size_in_mega_bytes(s_bv)
        + size_in_mega_bytes(f_bv)
        + size_in_mega_bytes(t_bv)
        + size_in_mega_bytes(i_iv)
        //+ size_in_mega_bytes(i_wt)
        + size_in_mega_bytes(s_cbv)
        + size_in_mega_bytes(h_iv)
        + size_in_mega_bytes(ts_iv)
        // TODO: add in size of the bs_arrays in a loop
        + paths_mb_size
        ) << endl;

#endif

    if (print_graph) {
        cerr << "printing graph" << endl;
        cerr << s_iv << endl;
        for (int i = 0; i < s_iv.size(); ++i) {
            cerr << revdna3bit(s_iv[i]);
        } cerr << endl;
        cerr << s_bv << endl;
        cerr << i_iv << endl;
        cerr << f_iv << endl;
        cerr << f_bv << endl;
        cerr << t_iv << endl;
        cerr << t_bv << endl;
        cerr << "paths" << endl;
        for (size_t i = 0; i < paths.size(); i++) {
            // Go through paths by number, so we can determine rank
            XGPath* path = paths[i];

            cerr << path_name(i + 1) << endl;
            cerr << path->members << endl;
            cerr << path->ids << endl;
            cerr << path->ranks << endl;
            cerr << path->directions << endl;
            cerr << path->positions << endl;
            cerr << path->offsets << endl;
        }
        cerr << ep_bv << endl;
        cerr << ep_iv << endl;
    }

    if (validate_graph) {
        cerr << "validating graph sequence" << endl;
        int max_id = s_cbv_rank(s_cbv.size());
        for (auto& p : node_label) {
            int64_t id = p.first;
            const string& l = p.second;
            //size_t rank = node_rank[id];
            size_t rank = id_to_rank(id);
            //cerr << rank << endl;
            // find the node in the array
            //cerr << "id = " << id << " rank = " << s_cbv_select(rank) << endl;
            // this should be true given how we constructed things
            if (rank != s_cbv_rank(s_cbv_select(rank)+1)) {
                cerr << rank << " != " << s_cbv_rank(s_cbv_select(rank)+1) << " for node " << id << endl;
                assert(false);
            }
            // get the sequence from the s_iv
            string s = node_sequence(id);

            string ltmp, stmp;
            if (l.size() != s.size()) {
                cerr << l << " != " << endl << s << endl << " for node " << id << endl;
                assert(false);
            } else {
                int j = 0;
                for (auto c : l) {
                    if (dna3bit(c) != dna3bit(s[j++])) {
                        cerr << l << " != " << endl << s << endl << " for node " << id << endl;
                        assert(false);
                    }
                }
            }
        }
        node_label.clear();

        // -1 here seems weird
        // what?
        cerr << "validating forward edge table" << endl;
        for (size_t j = 0; j < f_iv.size()-1; ++j) {
            if (f_bv[j] == 1) continue;
            // from id == rank
            size_t fid = i_iv[f_bv_rank(j)-1];
            // to id == f_cbv[j]
            size_t tid = i_iv[f_iv[j]-1];
            bool from_start = f_from_start_bv[j];
            // get the to_end
            bool to_end = false;
            for (auto& side : from_to[make_side(fid, from_start)]) {
                if (side_id(side) == tid) {
                    to_end = side_is_end(side);
                }
            }
            if (from_to[make_side(fid, from_start)].count(make_side(tid, to_end)) == 0) {
                cerr << "could not find edge (f) "
                     << fid << (from_start ? "+" : "-")
                     << " -> "
                     << tid << (to_end ? "+" : "-")
                     << endl;
                assert(false);
            }
        }

        cerr << "validating reverse edge table" << endl;
        for (size_t j = 0; j < t_iv.size()-1; ++j) {
            //cerr << j << endl;
            if (t_bv[j] == 1) continue;
            // from id == rank
            size_t tid = i_iv[t_bv_rank(j)-1];
            // to id == f_cbv[j]
            size_t fid = i_iv[t_iv[j]-1];
            //cerr << tid << " " << fid << endl;

            bool to_end = t_to_end_bv[j];
            // get the to_end
            bool from_start = false;
            for (auto& side : to_from[make_side(tid, to_end)]) {
                if (side_id(side) == fid) {
                    from_start = side_is_end(side);
                }
            }
            if (to_from[make_side(tid, to_end)].count(make_side(fid, from_start)) == 0) {
                cerr << "could not find edge (t) "
                     << fid << (from_start ? "+" : "-")
                     << " -> "
                     << tid << (to_end ? "+" : "-")
                     << endl;
                assert(false);
            }
        }

        cerr << "validating paths" << endl;
        for (auto& pathpair : path_nodes) {
            const string& name = pathpair.first;
            auto& path = pathpair.second;
            size_t prank = path_rank(name);
            //cerr << path_name(prank) << endl;
            assert(path_name(prank) == name);
            sd_vector<>& pe_bv = paths[prank-1]->members;
            int_vector<>& pp_iv = paths[prank-1]->positions;
            sd_vector<>& dir_bv = paths[prank-1]->directions;
            // check each entity in the nodes is present
            // and check node reported at the positions in it
            size_t pos = 0;
            size_t in_path = 0;
            for (auto& m : path) {
                int64_t id = trav_id(m);
                bool rev = trav_is_rev(m);
                // todo rank
                assert(pe_bv[node_rank_as_entity(id)-1]);
                assert(dir_bv[in_path] == rev);
                Node n = node(id);
                //cerr << id << " in " << name << endl;
                auto p = node_positions_in_path(id, name);
                assert(std::find(p.begin(), p.end(), pos) != p.end());
                for (size_t k = 0; k < n.sequence().size(); ++k) {
                    //cerr << "id " << id << " ==? " << node_at_path_position(name, pos+k) << endl;
                    assert(id == node_at_path_position(name, pos+k));
                    assert(id == mapping_at_path_position(name, pos+k).position().node_id());
                }
                pos += n.sequence().size();
                ++in_path;
            }
            //cerr << path_name << " rank = " << prank << endl;
            // check membership now for each entity in the path
        }

#if GPBWT_MODE == MODE_SDSL
        if(store_threads && is_sorted_dag) {
#elif GPBWT_MODE == MODE_DYNAMIC
        if(store_threads) {
#endif

            cerr << "validating threads" << endl;

            // How many thread orientations are in the index?
            size_t threads_found = 0;
            // And how many shoukd we have inserted?
            size_t threads_expected = 0;

            for(auto thread : extract_threads()) {
#ifdef VERBOSE_DEBUG
                cerr << "Thread: ";
                for(size_t i = 0; i < thread.size(); i++) {
                    ThreadMapping mapping = thread[i];
                    cerr << mapping.node_id * 2 + mapping.is_reverse << "; ";
                }
                cerr << endl;
#endif
                // Make sure we can search all the threads we find present in the index
                assert(count_matches(thread) > 0);

                // Flip the thread around
                reverse(thread.begin(), thread.end());
                for(auto& mapping : thread) {
                    mapping.is_reverse = !mapping.is_reverse;
                }

                // We need to be able to find it backwards as well
                assert(count_matches(thread) > 0);

                threads_found++;
            }

            for (auto& pathpair : path_nodes) {
                Path reconstructed;

                // Grab the name
                reconstructed.set_name(pathpair.first);

                // This path should have been inserted. Look for it.
                assert(count_matches(reconstructed) > 0);

                threads_expected += 2;

            }

            // Make sure we have the right number of threads.
            assert(threads_found == threads_expected);
        }

        cerr << "graph ok" << endl;
    }
}

Node XG::node(int64_t id) const {
    Node n;
    n.set_id(id);
    //cerr << omp_get_thread_num() << " looks for " << id << endl;
    n.set_sequence(node_sequence(id));
    return n;
}

string XG::node_sequence(int64_t id) const {
    size_t rank = id_to_rank(id);
    size_t start = s_cbv_select(rank);
    size_t end = rank == node_count ? s_cbv.size() : s_cbv_select(rank+1);
    string s; s.resize(end-start);
    for (size_t i = start; i < s_cbv.size() && i < end; ++i) {
        s[i-start] = revdna3bit(s_iv[i]);
    }
    return s;
}

size_t XG::node_length(int64_t id) const {
    size_t rank = id_to_rank(id);
    size_t start = s_cbv_select(rank);
    size_t end = rank == node_count ? s_cbv.size() : s_cbv_select(rank+1);
    return end-start;
}

char XG::pos_char(int64_t id, bool is_rev, size_t off) const {
    assert(off < node_length(id));
    if (!is_rev) {
        size_t rank = id_to_rank(id);
        size_t pos = s_cbv_select(rank) + off;
        assert(pos < s_iv.size());
        char c = revdna3bit(s_iv[pos]);
        return c;
    } else {
        size_t rank = id_to_rank(id);
        size_t pos = s_cbv_select(rank+1) - (off+1);
        assert(pos < s_iv.size());
        char c = revdna3bit(s_iv[pos]);
        return reverse_complement(c);
    }
}

string XG::pos_substr(int64_t id, bool is_rev, size_t off, size_t len) const {
    if (!is_rev) {
        size_t rank = id_to_rank(id);
        size_t start = s_cbv_select(rank) + off;
        assert(start < s_iv.size());
        // get until the end position, or the end of the node, which ever is first
        size_t end;
        if (!len) {
            end = s_cbv_select(rank+1);
        } else {
            end = min(start + len, (size_t)s_cbv_select(rank+1));
        }
        assert(end < s_iv.size());
        string s; s.resize(end-start);
        for (size_t i = start; i < s_cbv.size() && i < end; ++i) {
            s[i-start] = revdna3bit(s_iv[i]);
        }
        return s;
    } else {
        size_t rank = id_to_rank(id);
        size_t end = s_cbv_select(rank+1) - off;
        assert(end < s_iv.size());
        // get until the end position, or the end of the node, which ever is first
        size_t start;
        if (len > end || !len) {
            start = s_cbv_select(rank);
        } else {
            start = max(end - len, (size_t)s_cbv_select(rank));
        }
        assert(end < s_iv.size());
        string s; s.resize(end-start);
        for (size_t i = start; i < s_cbv.size() && i < end; ++i) {
            s[i-start] = revdna3bit(s_iv[i]);
        }
        return reverse_complement(s);
    }
}

size_t XG::id_to_rank(int64_t id) const {
    return r_iv[id-min_id];
}

int64_t XG::rank_to_id(size_t rank) const {
    if(rank == 0) {
        cerr << "[xg] error: Request for id of rank 0" << endl;
        exit(1);
    }
    if(rank > i_iv.size()) {
        cerr << "[xg] error: Request for id of rank " << rank << "/" << i_iv.size() << endl;
        exit(1);
    }
    return i_iv[rank-1];
}

vector<Edge> XG::edges_of(int64_t id) const {
    auto e1 = edges_to(id);
    auto e2 = edges_from(id);
    e1.reserve(e1.size() + distance(e2.begin(), e2.end()));
    e1.insert(e1.end(), e2.begin(), e2.end());
    // now get rid of duplicates
    vector<Edge> e3;
    set<string> seen;
    for (auto& edge : e1) {
        string s; edge.SerializeToString(&s);
        if (!seen.count(s)) {
            e3.push_back(edge);
            seen.insert(s);
        }
    }
    return e3;
}

vector<Edge> XG::edges_to(int64_t id) const {
    vector<Edge> edges;
    size_t rank = id_to_rank(id);
    size_t t_start = t_bv_select(rank)+1;
    size_t t_end = rank == node_count ? t_bv.size() : t_bv_select(rank+1);
    for (size_t i = t_start; i < t_end; ++i) {
        Edge edge;
        edge.set_to(id);
        edge.set_from(rank_to_id(t_iv[i]));
        edge.set_from_start(t_from_start_cbv[i]);
        edge.set_to_end(t_to_end_cbv[i]);
        edges.push_back(edge);
    }
    return edges;
}

vector<Edge> XG::edges_from(int64_t id) const {
    vector<Edge> edges;
    size_t rank = id_to_rank(id);
    size_t f_start = f_bv_select(rank)+1;
    size_t f_end = rank == node_count ? f_bv.size() : f_bv_select(rank+1);
    for (size_t i = f_start; i < f_end; ++i) {
        Edge edge;
        edge.set_from(id);
        edge.set_to(rank_to_id(f_iv[i]));
        edge.set_from_start(f_from_start_cbv[i]);
        edge.set_to_end(f_to_end_cbv[i]);
        edges.push_back(edge);
    }
    return edges;
}

vector<Edge> XG::edges_on_start(int64_t id) const {
    vector<Edge> edges;
    for (auto& edge : edges_of(id)) {
        if((edge.to() == id && !edge.to_end()) || (edge.from() == id && edge.from_start())) {
            edges.push_back(edge);
        }
    }
    return edges;
}

vector<Edge> XG::edges_on_end(int64_t id) const {
    vector<Edge> edges;
    for (auto& edge : edges_of(id)) {
        if((edge.to() == id && edge.to_end()) || (edge.from() == id && !edge.from_start())) {
            edges.push_back(edge);
        }
    }
    return edges;
}

size_t XG::max_node_rank(void) const {
    return s_cbv_rank(s_cbv.size());
}

int64_t XG::node_at_seq_pos(size_t pos) const {
    return rank_to_id(s_cbv_rank(pos));
}

size_t XG::node_start(int64_t id) const {
    return s_cbv_select(id_to_rank(id));
}

size_t XG::max_path_rank(void) const {
    //cerr << pn_bv << endl;
    //cerr << "..." << pn_bv_rank(pn_bv.size()) << endl;
    return pn_bv_rank(pn_bv.size());
}

size_t XG::node_rank_as_entity(int64_t id) const {
    //cerr << id_to_rank(id) << endl;
    return f_bv_select(id_to_rank(id))+1;
}

bool XG::entity_is_node(size_t rank) const {
    return 1 == f_bv[rank-1];
}

size_t XG::entity_rank_as_node_rank(size_t rank) const {
    return !entity_is_node(rank) ? 0 : f_iv[rank-1];
}

// snoop through the forward table to check if the edge exists
bool XG::has_edge(int64_t id1, bool from_start, int64_t id2, bool to_end) const {
    // invert the edge if we are doubly-reversed
    // this has the same meaning
    // ...confused
    /*
    if (from_start && to_end) {
        int64_t tmp = id1;
        id1 = id2; id2 = tmp;
        from_start = false;
        to_end = false;
    }
    */
    size_t rank1 = id_to_rank(id1);
    size_t rank2 = id_to_rank(id2);
    // Start looking after the value that corresponds to the node itself.
    // Otherwise we'll think every self loop exists.
    size_t f_start = f_bv_select(rank1) + 1;
    size_t f_end = rank1 == node_count ? f_bv.size() : f_bv_select(rank1+1);
    for (size_t i = f_start; i < f_end; ++i) {
        if (rank2 == f_iv[i]
            && f_from_start_cbv[i] == from_start
            && f_to_end_cbv[i] == to_end) {
            return true;
        }
    }
    return false;
}

int64_t XG::node_height(XG::ThreadMapping node) const {
  return h_iv[(node_rank_as_entity(node.node_id) - 1) * 2 + node.is_reverse];
}

int64_t XG::threads_starting_at_node(ThreadMapping node) const {
  return ts_iv[(node_rank_as_entity(node.node_id) - 1) * 2 + node.is_reverse];
}

size_t XG::edge_rank_as_entity(int64_t id1, bool from_start, int64_t id2, bool to_end) const {
    size_t rank1 = id_to_rank(id1);
    size_t rank2 = id_to_rank(id2);
#ifdef VERBOSE_DEBUG
    cerr << "Finding rank for "
         << id1 << (from_start?"+":"-") << " (" << rank1 << ") " << " -> "
         << id2 << (to_end?"-":"+") << " (" << rank2 << ")"<< endl;
#endif
    size_t f_start = f_bv_select(rank1) + 1;
    size_t f_end = rank1 == node_count ? f_bv.size() : f_bv_select(rank1+1);
#ifdef VERBOSE_DEBUG
    cerr << f_start << " to " << f_end << endl;
#endif
    for (size_t i = f_start; i < f_end; ++i) {
#ifdef VERBOSE_DEBUG
        cerr << f_iv[i] << endl;
#endif
        if (rank2 == f_iv[i]
            && f_from_start_cbv[i] == from_start
            && f_to_end_cbv[i] == to_end) {
#ifdef VERBOSE_DEBUG
            cerr << i << endl;
#endif
            return i+1;
        }
    }
    //cerr << "edge does not exist: " << id1 << " -> " << id2 << endl;
    assert(false);
}

size_t XG::edge_rank_as_entity(const Edge& edge) const {
    if(has_edge(edge.from(), edge.from_start(), edge.to(), edge.to_end())) {
        int64_t rank = edge_rank_as_entity(edge.from(), edge.from_start(), edge.to(), edge.to_end());
#ifdef VERBOSE_DEBUG
        cerr << "Found rank " << rank << endl;
#endif
        assert(!entity_is_node(rank));
        return rank;
    } else if(has_edge(edge.to(), !edge.to_end(), edge.from(), !edge.from_start())) {
        // Handle the case where the edge is spelled backwards; get the rank of the forwards version.
        int64_t rank = edge_rank_as_entity(edge.to(), !edge.to_end(), edge.from(), !edge.from_start());
#ifdef VERBOSE_DEBUG
        cerr << "Found rank " << rank << endl;
#endif
        assert(!entity_is_node(rank));
        return rank;
    } else {
        // Someone gave us an edge that doesn't exist.
        assert(false);
    }
}

Edge XG::canonicalize(const Edge& edge) {
    if(has_edge(edge.from(), edge.from_start(), edge.to(), edge.to_end())) {
        return edge;
    } else {
        return make_edge(edge.to(), !edge.to_end(), edge.from(), !edge.from_start());
    }
}

Path XG::path(const string& name) const {
    // Extract a whole path by name

    // First find the XGPath we're using to store it.
    XGPath& xgpath = *(paths[path_rank(name)-1]);

    // Make a new path to fill in
    Path to_return;
    // Fill in the name
    to_return.set_name(name);

    // There's one ID entry per node visit
    size_t total_nodes = xgpath.ids.size();

    for(size_t i = 0; i < total_nodes; i++) {
        // For everything on the XGPath, put a Mapping on the real path.
        *(to_return.add_mapping()) = xgpath.mapping(i);
    }

    return to_return;

}

size_t XG::path_rank(const string& name) const {
    // find the name in the csa
    string query = start_marker + name + end_marker;
    auto occs = locate(pn_csa, query);
    if (occs.size() > 1) {
        cerr << "multiple hits for " << query << endl;
        assert(false);
    }
    if(occs.size() == 0) {
        // This path does not exist. Give back 0, which can never be a real path
        // rank.
        return 0;
    }
    //cerr << "path named " << name << " is at " << occs[0] << endl;
    return pn_bv_rank(occs[0])+1; // step past '#'
}

string XG::path_name(size_t rank) const {
    //cerr << "path rank " << rank << endl;
    size_t start = pn_bv_select(rank)+1; // step past '#'
    size_t end = rank == path_count ? pn_iv.size() : pn_bv_select(rank+1);
    end -= 1;  // step before '$'
    string name; name.resize(end-start);
    for (size_t i = start; i < end; ++i) {
        name[i-start] = pn_iv[i];
    }
    return name;
}

bool XG::path_contains_entity(const string& name, size_t rank) const {
    return 1 == paths[path_rank(name)-1]->members[rank-1];
}

bool XG::path_contains_node(const string& name, int64_t id) const {
    return path_contains_entity(name, node_rank_as_entity(id));
}

bool XG::path_contains_edge(const string& name, int64_t id1, bool from_start, int64_t id2, bool to_end) const {
    return path_contains_entity(name, edge_rank_as_entity(id1, from_start, id2, to_end));
}

vector<size_t> XG::paths_of_entity(size_t rank) const {
    size_t off = ep_bv_select(rank);
    assert(ep_bv[off++]);
    vector<size_t> path_ranks;
    while (off < ep_bv.size() && ep_bv[off] == 0) {
        path_ranks.push_back(ep_iv[off++]);
    }
    return path_ranks;
}

vector<size_t> XG::paths_of_node(int64_t id) const {
    return paths_of_entity(node_rank_as_entity(id));
}

vector<size_t> XG::paths_of_edge(int64_t id1, bool from_start, int64_t id2, bool to_end) const {
    return paths_of_entity(edge_rank_as_entity(id1, from_start, id2, to_end));
}

map<string, vector<Mapping>> XG::node_mappings(int64_t id) const {
    map<string, vector<Mapping>> mappings;
    // for each time the node crosses the path
    for (auto i : paths_of_entity(node_rank_as_entity(id))) {
        // get the path name
        string name = path_name(i);
        // get reference to the offset of the mapping in the path
        // to get the direction and (stored) rank
        for (auto j : node_ranks_in_path(id, name)) {
            // nb: path rank is 1-based, path index is 0-based
            mappings[name].push_back(paths[i-1]->mapping(j));
        }
    }
    return mappings;
}

void XG::neighborhood(int64_t id, size_t dist, Graph& g, bool use_steps) const {
    *g.add_node() = node(id);
    expand_context(g, dist, true, use_steps);
}

void XG::expand_context(Graph& g, size_t dist, bool add_paths, bool use_steps) const {
    if (use_steps) {
        expand_context_by_steps(g, dist, add_paths);
    } else {
        expand_context_by_length(g, dist, add_paths);
    }
}
void XG::expand_context_by_steps(Graph& g, size_t steps, bool add_paths) const {
    map<int64_t, Node*> nodes;
    map<pair<side_t, side_t>, Edge*> edges;
    set<int64_t> to_visit;
    // start with the nodes in the graph
    for (size_t i = 0; i < g.node_size(); ++i) {
        to_visit.insert(g.node(i).id());
        // handles the single-node case: we should still get the paths
        Node* np = g.mutable_node(i);
        nodes[np->id()] = np;
    }
    for (size_t i = 0; i < g.edge_size(); ++i) {
        auto& edge = g.edge(i);
        to_visit.insert(edge.from());
        to_visit.insert(edge.to());
        edges[make_pair(make_side(edge.from(), edge.from_start()),
                        make_side(edge.to(), edge.to_end()))] = g.mutable_edge(i);
    }
    // and expand
    for (size_t i = 0; i < steps; ++i) {
        set<int64_t> to_visit_next;
        for (auto id : to_visit) {
            // build out the graph
            // if we have nodes we haven't seeen
            if (nodes.find(id) == nodes.end()) {
                Node* np = g.add_node();
                nodes[id] = np;
                *np = node(id);
            }
            for (auto& edge : edges_of(id)) {
                auto sides = make_pair(make_side(edge.from(), edge.from_start()),
                                       make_side(edge.to(), edge.to_end()));
                if (edges.find(sides) == edges.end()) {
                    Edge* ep = g.add_edge(); *ep = edge;
                    edges[sides] = ep;
                }
                if (edge.from() == id) {
                    to_visit_next.insert(edge.to());
                } else {
                    to_visit_next.insert(edge.from());
                }
            }
        }
        to_visit = to_visit_next;
    }
    // then add connected nodes that we have edges to but didn't pull in yet.
    // These are the nodes reached on the last step; we won't follow their edges
    // to new noded.
    set<int64_t> last_step_nodes;
    for (auto& e : edges) {
        auto& edge = e.second;
        // get missing nodes
        int64_t f = edge->from();
        if (nodes.find(f) == nodes.end()) {
            Node* np = g.add_node();
            nodes[f] = np;
            *np = node(f);
            last_step_nodes.insert(f);
        }
        int64_t t = edge->to();
        if (nodes.find(t) == nodes.end()) {
            Node* np = g.add_node();
            nodes[t] = np;
            *np = node(t);
            last_step_nodes.insert(t);
        }
    }
    // We do need to find edges that connect the nodes we just grabbed on the
    // last step. Otherwise we'll produce something that isn't really a useful
    // subgraph, because there might be edges connecting the nodes you have that
    // you don't see.
    for(auto& n : last_step_nodes) {
        for (auto& edge : edges_from(n)) {
            if(last_step_nodes.count(edge.to())) {
                // This edge connects two nodes that were added on the last
                // step, and so wouldn't have been found by the main loop.

                // Determine if it's been seen already (somehow).
                // TODO: it probably shouldn't have been, unless it's a self loop or something.
                auto sides = make_pair(make_side(edge.from(), edge.from_start()),
                                       make_side(edge.to(), edge.to_end()));
                if (edges.find(sides) == edges.end()) {
                    // If it isn't there already, add it to the graph
                    Edge* ep = g.add_edge(); *ep = edge;
                    edges[sides] = ep;
                }
            }
        }
    }
    // Edges between the last step nodes and other nodes will have already been
    // pulled in, on the step when those other nodes were processed by the main
    // loop.
    if (add_paths) {
        add_paths_to_graph(nodes, g);
    }
}

void XG::expand_context_by_length(Graph& g, size_t length, bool add_paths) const {

    // map node_id --> min-distance-to-left-side, min-distance-to-right-side
    // these distances include the length of the node in the table.
    map<int64_t, pair<int64_t, int64_t> > node_table;
    // nodes and edges in graph, so we don't duplicate when we add to protobuf
    map<int64_t, Node*> nodes;
    map<pair<side_t, side_t>, Edge*> edges;
    // bfs queue (id, enter-on-left-size)
    queue<int64_t> to_visit;

    // add starting graph with distance 0
    for (size_t i = 0; i < g.node_size(); ++i) {
        Node* np = g.mutable_node(i);
        node_table[np->id()] = pair<int64_t, int64_t>(0, 0);
        nodes[np->id()] = np;
        to_visit.push(np->id());
    }

    // add starting edges
    for (size_t i = 0; i < g.edge_size(); ++i) {
        auto& edge = g.edge(i);
        edges[make_pair(make_side(edge.from(), edge.from_start()),
                        make_side(edge.to(), edge.to_end()))] = g.mutable_edge(i);
    }

    // expand outward breadth-first
    while (!to_visit.empty()) {
        int64_t id = to_visit.front();
        to_visit.pop();
        pair<int64_t, int64_t> dists = node_table[id];
        if (dists.first < length || dists.second < length) {
            for (auto& edge : edges_of(id)) {
                // update distance table with other end of edge
                function<void(int64_t, bool, bool)> lambda = [&](
                    int64_t other, bool from_start, bool to_end) {

                    int64_t dist = !from_start ? dists.first : dists.second;
                    int64_t other_dist = dist + node_length(other);
                    if (dist < length) {
                        auto it = node_table.find(other);
                        bool updated = false;
                        if (it == node_table.end()) {
                            auto entry = make_pair(numeric_limits<int64_t>::max(),
                                                   numeric_limits<int64_t>::max());
                            it = node_table.insert(make_pair(other, entry)).first;
                            updated = true;
                        }
                        if (!to_end && other_dist < it->second.first) {
                            updated = true;
                            node_table[other].first = other_dist;
                        } else if (to_end && other_dist < it->second.second) {
                            updated = true;
                            node_table[other].second = other_dist;
                        }
                        // create the other node
                        if (nodes.find(other) == nodes.end()) {
                            Node* np = g.add_node();
                            nodes[other] = np;
                            *np = node(other);
                        }
                        // create all links back to graph, so as not to break paths
                        for (auto& other_edge : edges_of(other)) {
                            auto sides = make_pair(make_side(other_edge.from(),
                                                             other_edge.from_start()),
                                                   make_side(other_edge.to(),
                                                             other_edge.to_end()));
                            int64_t other_from = other_edge.from() == other ? other_edge.to() :
                                other_edge.from();
                            if (nodes.find(other_from) != nodes.end() &&
                                edges.find(sides) == edges.end()) {
                                Edge* ep = g.add_edge(); *ep = other_edge;
                                edges[sides] = ep;
                            }
                        }
                        // revisit the other node
                        if (updated) {
                            // this may be overly conservative (bumping any updated node)
                            to_visit.push(other);
                        }
                    }
                };
                // we can actually do two updates if we have a self loop, hence no else below
                if (edge.from() == id) {
                    lambda(edge.to(), edge.from_start(), edge.to_end());
                }
                if (edge.to() == id) {
                    lambda(edge.from(), !edge.to_end(), !edge.from_start());
                }
            }
        }
    }

    if (add_paths) {
        add_paths_to_graph(nodes, g);
    }
}

// if the graph ids partially ordered, this works no prob
// otherwise... owch
// the paths become disordered due to traversal of the node ids in order
void XG::add_paths_to_graph(map<int64_t, Node*>& nodes, Graph& g) const {
    // map from path name to (map from mapping rank to mapping)
    map<string, map<size_t, Mapping>> paths;
    // mappings without
    map<string, vector<Mapping>> unplaced;
    // use:
    //size_t node_position_in_path(int64_t id, const string& name) const;

    // pick up current paths in the graph
    for (size_t i = 0; i < g.path_size(); ++i) {
        auto& path = g.path(i);
        for (size_t j = 0; j < path.mapping_size(); ++j) {
            auto& m = path.mapping(j);
            if (m.rank()) {
                paths[path.name()][m.rank()] = m;
            } else {
                unplaced[path.name()].push_back(m);
            }
        }
    }
    // do the same for the mappings in the list of nodes
    for (auto& n : nodes) {
        auto& id = n.first;
        auto& node = n.second;
        for (auto& n : node_mappings(id)) {
            auto& name = n.first;
            for (auto& m : n.second) {
                if (m.rank()) {
                    paths[name][m.rank()] = m;
                } else {
                    unplaced[name].push_back(m);
                }
            }
        }
    }
    // rebuild graph's paths
    // NB: mapping ranks allow us to remove this bit
    // only adding what we haven't seen before
    g.clear_path();
    for (auto& p : paths) {
        auto& name = p.first;
        auto& mappings = p.second;
        Path* path = g.add_path();
        path->set_name(name);
        for (auto& n : mappings) {
            *path->add_mapping() = n.second;
        }
        if (unplaced.find(name) != unplaced.end()) {
            auto& unp = unplaced[name];
            for (auto& m : unp) {
                *path->add_mapping() = m;
            }
        }
    }
}

void XG::get_id_range(int64_t id1, int64_t id2, Graph& g) const {
    id1 = max(min_id, id1);
    id2 = min(max_id, id2);
    for (auto i = id1; i <= id2; ++i) {
        *g.add_node() = node(i);
    }
}

// walk forward in id space, collecting nodes, until at least length bases covered
// (or end of graph reached).  if forward is false, do go backward
void XG::get_id_range_by_length(int64_t id, int64_t length, Graph& g, bool forward) const {
    // find out first base of node's position in the sequence vector
    size_t rank = id_to_rank(id);
    size_t start = s_cbv_select(rank);
    size_t end;
    // jump by length, checking to make sure we stay in bounds
    if (forward) {
        end = s_cbv_rank(min(s_cbv.size() - 1, start + node_length(id) + length));
    } else {
        end = s_cbv_rank(1 + max((int64_t)0, (int64_t)(start  - length)));
    }
    // convert back to id
    int64_t id2 = rank_to_id(end);

    // get the id range
    if (!forward) {
        swap(id, id2);
    }
    get_id_range(id, id2, g);
}


/*
void XG::get_connected_nodes(Graph& g) {
}
*/

size_t XG::path_length(const string& name) const {
    return paths[path_rank(name)-1]->offsets.size();
}

// if node is on path, return it.  otherwise, return next node (in id space)
// that is on path.  if none exists, return 0
int64_t XG::next_path_node_by_id(size_t path_rank, int64_t id) const {

    // find our node in the members bit vector of the xgpath
    XGPath* path = paths[path_rank - 1];
    size_t node_rank = id_to_rank(id);
    size_t entity_rank = node_rank_as_entity(node_rank);
    // if it's a path member, we're done
    if (path->members[entity_rank - 1] == 1) {
        return id;
    }

    // note: rank select in members sd_vector is O(log[|graph| / |path|])
    // so this will be relatively slow on tiny paths (but so will alternatives
    // like checking path->members on every node):

    // find number of members before our node in the path
    size_t members_rank_at_node = path->members_rank(entity_rank - 1);
    // next member doesn't exist
    if (members_rank_at_node == path->members.size()) {
        return 0;
    }
    // hop to the next member
    int64_t i = path->members_select(members_rank_at_node + 1);

    // sanity check that we're at a node member
    assert(f_bv[i] == 1 && path->members[i] == 1);

    // convert from entity_rank back to node id
    int64_t next_id = entity_rank_as_node_rank(i + 1);

    return next_id;
}

// if node is on path, return it.  otherwise, return previous node (in id space)
// that is on path.  if none exists, return 0
int64_t XG::prev_path_node_by_id(size_t path_rank, int64_t id) const {

    // find our node in the members bit vector of the xgpath
    XGPath* path = paths[path_rank - 1];
    size_t node_rank = id_to_rank(id);
    size_t entity_rank = node_rank_as_entity(node_rank);
    // if it's a path member, we're done
    if (path->members[entity_rank - 1] == 1) {
        return id;
    }

    // note: rank select in members sd_vector is O(log[|graph| / |path|])
    // so this will be relatively slow on tiny paths (but so will alternatives
    // like checking path->members on every node):

    // find number of members before our node in the path
    size_t members_rank_at_node = path->members_rank(entity_rank - 1);
    // previous member doesn't exist
    if (members_rank_at_node == 0) {
        return 0;
    }
    // hop to the previous member
    int64_t i = path->members_select(members_rank_at_node);
    // skip edges till we hit previous node
    for (; i >= 0 && f_bv[i] == 0; --i);

    // sanity check that we're at a node member
    assert(i >= 0 && f_bv[i] == 1 && path->members[i] == 1);

    // convert from entity_rank back to node id
    int64_t prev_id = entity_rank_as_node_rank(i + 1);

    return prev_id;
}

// estimate distance (in bp) between two nodes along a path.
// if a nodes isn't on the path, the nearest node on the path (using id space)
// is used as a proxy.  In this case, the distance may not be exact
// due to this heuristic, but should be sufficient for our purposese (evaluating
// pair consistency).
// returns -1 if couldn't find distance
int64_t XG::approx_path_distance(const string& name, int64_t id1, int64_t id2) const {
    // simplifying assumption: id1 lies before id2 on path (and id space)
    if (id1 > id2) {
        swap(id1, id2);
    }
    size_t path_rank = this->path_rank(name);
    int64_t next1 = next_path_node_by_id(path_rank, id1);
    int64_t prev2 = prev_path_node_by_id(path_rank, id2);

    // fail
    if (next1 == 0 || prev2 == 0) {
        assert(false); // for now
        return -1;
    }

    // find our positions on the path
    vector<size_t> positions1 = node_positions_in_path(next1, name);
    vector<size_t> positions2 = node_positions_in_path(prev2, name);
    // use the last node1 position and first node2 position.
    int64_t pos1 = (int64_t)positions1.back();
    int64_t pos2 = (int64_t)positions2[0];
    // shift over to the right of the left node if it's unchanged
    if (next1 == id1) {
        pos1 += node_length(next1);
    }

    return abs(pos2 - pos1);
}

// like above, but find minumum over list of paths.  if names is empty, do all paths
// don't actually take strict minumum over all paths.  rather, prefer paths that
// contain the nodes when possible.
int64_t XG::min_approx_path_distance(const vector<string>& names,
                                     int64_t id1, int64_t id2) const {
    vector<int64_t> min_distance(3, numeric_limits<int64_t>::max());

    function<void(const string&)> lambda =[&](const string& name) {
        int member1 = path_contains_node(name, id1) ? 1 : 0;
        int member2 = path_contains_node(name, id2) ? 1 : 0;
        int md_idx = member1 + member2;

        if (md_idx == 2 ||
            (md_idx == 1 && min_distance[2] == numeric_limits<int64_t>::max()) ||
            (md_idx == 0 && min_distance[2] == numeric_limits<int64_t>::max() &&
             min_distance[1] == numeric_limits<int64_t>::max())) {

            int64_t dist = approx_path_distance(name, id1, id2);
            if (dist >= 0 && dist < min_distance[md_idx]) {
                min_distance[md_idx] = dist;
            }
        }
    };

    if (names.size() > 1) {
        for (const string& name : names) {
            lambda(name);
        }
    } else {
        size_t max_path_rank = this->max_path_rank();
        for (size_t i = 1; i <= max_path_rank; ++i) {
            lambda(path_name(i));
        }
    }

    if (min_distance[2] != numeric_limits<int64_t>::max()) {
        return min_distance[2];
    } else if (min_distance[1] != numeric_limits<int64_t>::max()) {
        return min_distance[1];
    } else if (min_distance[0] != numeric_limits<int64_t>::max()) {
        return min_distance[0];
    }
    return -1;
}

// TODO, include paths
void XG::get_path_range(string& name, int64_t start, int64_t stop, Graph& g) const {
    // what is the node at the start, and at the end
    auto& path = *paths[path_rank(name)-1];
    size_t plen = path.offsets.size();
    if (start > plen) return; // no overlap with path
    size_t pr1 = path.offsets_rank(start+1)-1;
    // careful not to exceed the path length
    if (stop >= plen) stop = plen-1;
    size_t pr2 = path.offsets_rank(stop+1)-1;
    set<int64_t> nodes;
    set<pair<side_t, side_t> > edges;
    // Grab the IDs visited in order along the path
    auto& pi_wt = path.ids;
    for (size_t i = pr1; i <= pr2; ++i) {
        // For all the visits along this section of path, grab the node being visited and all its edges.
        int64_t id = pi_wt[i];
        nodes.insert(id);
        for (auto& e : edges_from(id)) {
            edges.insert(make_pair(make_side(e.from(), e.from_start()), make_side(e.to(), e.to_end())));
        }
        for (auto& e : edges_to(id)) {
            edges.insert(make_pair(make_side(e.from(), e.from_start()), make_side(e.to(), e.to_end())));
        }
    }
    for (auto& n : nodes) {
        *g.add_node() = node(n);
    }
    map<string, Path*> local_paths;
    for (auto& n : nodes) {
        for (auto& m : node_mappings(n)) {
            if (local_paths.find(m.first) == local_paths.end()) {
                Path* p = g.add_path();
                local_paths[m.first] = p;
                p->set_name(m.first);
            }
            Path* new_path = local_paths[m.first];
            // TODO output mapping direction
            //if () { m.second.is_reverse(true); }
            for (auto& n : m.second) {
                *new_path->add_mapping() = n;
            }
        }
    }
    for (auto& e : edges) {
        Edge edge;
        edge.set_from(side_id(e.first));
        edge.set_from_start(side_is_end(e.first));
        edge.set_to(side_id(e.second));
        edge.set_to_end(side_is_end(e.second));
        *g.add_edge() = edge;
    }
}

size_t XG::node_occs_in_path(int64_t id, const string& name) const {
    return node_occs_in_path(id, path_rank(name));
}

size_t XG::node_occs_in_path(int64_t id, size_t rank) const {
    size_t p = rank-1;
    auto& pi_wt = paths[p]->ids;
    return pi_wt.rank(pi_wt.size(), id);
}

vector<size_t> XG::node_ranks_in_path(int64_t id, const string& name) const {
    return node_ranks_in_path(id, path_rank(name));
}

vector<size_t> XG::node_ranks_in_path(int64_t id, size_t rank) const {
    vector<size_t> ranks;
    size_t p = rank-1;
    for (size_t i = 1; i <= node_occs_in_path(id, rank); ++i) {
#pragma omp critical(ids_select)
        ranks.push_back(paths[p]->ids.select(i, id));
        auto m = paths[p]->mapping(ranks.back());
    }
    return ranks;
}

vector<size_t> XG::node_positions_in_path(int64_t id, const string& name) const {
    return node_positions_in_path(id, path_rank(name));
}

vector<size_t> XG::node_positions_in_path(int64_t id, size_t rank) const {
    auto& path = *paths[rank-1];
    vector<size_t> pos_in_path;
    for (auto i : node_ranks_in_path(id, rank)) {
        pos_in_path.push_back(path.positions[i]);
    }
    return pos_in_path;
}

map<string, vector<size_t> > XG::node_positions_in_paths(int64_t id) const {
    map<string, vector<size_t> > positions;
    for (auto& prank : paths_of_node(id)) {
        auto& path = *paths[prank-1];
        auto& pos_in_path = positions[path_name(prank)];
        for (auto i : node_ranks_in_path(id, prank)) {
            pos_in_path.push_back(path.positions[i]);
        }
    }
    return positions;
}

int64_t XG::node_at_path_position(const string& name, size_t pos) const {
    size_t p = path_rank(name)-1;
    return paths[p]->ids[paths[p]->offsets_rank(pos+1)-1];
}

Mapping XG::mapping_at_path_position(const string& name, size_t pos) const {
    size_t p = path_rank(name)-1;
    return paths[p]->mapping(paths[p]->offsets_rank(pos+1)-1);
}

Mapping new_mapping(const string& name, int64_t id, size_t rank, bool is_reverse) {
    Mapping m;
    m.mutable_position()->set_node_id(id);
    m.mutable_position()->set_is_reverse(is_reverse);
    m.set_rank(rank);
    return m;
}

void parse_region(const string& target, string& name, int64_t& start, int64_t& end) {
    start = -1;
    end = -1;
    size_t foundFirstColon = target.find(":");
    // we only have a single string, use the whole sequence as the target
    if (foundFirstColon == string::npos) {
        name = target;
    } else {
        name = target.substr(0, foundFirstColon);
	    size_t foundRangeDash = target.find("-", foundFirstColon);
        if (foundRangeDash == string::npos) {
            start = atoi(target.substr(foundFirstColon + 1).c_str());
            end = start;
        } else {
            start = atoi(target.substr(foundFirstColon + 1, foundRangeDash - foundRangeDash - 1).c_str());
            end = atoi(target.substr(foundRangeDash + 1).c_str());
        }
    }
}

void to_text(ostream& out, Graph& graph) {
    out << "H" << "\t" << "HVN:Z:1.0" << endl;
    for (size_t i = 0; i < graph.node_size(); ++i) {
        auto& node = graph.node(i);
        out << "S" << "\t" << node.id() << "\t" << node.sequence() << endl;
    }
    for (size_t i = 0; i < graph.path_size(); ++i) {
        auto& path = graph.path(i);
        for (size_t j = 0; j < path.mapping_size(); ++j) {
            auto& mapping = path.mapping(i);
            string orientation = mapping.position().is_reverse() ? "-" : "+";
            out << "P" << "\t" << mapping.position().node_id() << "\t" << path.name() << "\t"
                << mapping.rank() << "\t" << orientation << "\n";
        }
    }
    for (int i = 0; i < graph.edge_size(); ++i) {
        auto& edge = graph.edge(i);
        out << "L" << "\t" << edge.from() << "\t"
            << (edge.from_start() ? "-" : "+") << "\t"
            << edge.to() << "\t"
            << (edge.to_end() ? "-" : "+") << endl;
    }
}

int64_t XG::where_to(int64_t current_side, int64_t visit_offset, int64_t new_side) const {
    // Given that we were at visit_offset on the current side, where will we be
    // on the new side?

    // What will the new visit offset be?
    int64_t new_visit_offset = 0;

    // Work out where we're going as a node and orientation
    int64_t new_node_id = rank_to_id(new_side / 2);
    bool new_node_is_reverse = new_side % 2;

    // Work out what edges are going into the place we're going into.
    vector<Edge> edges = new_node_is_reverse ? edges_on_end(new_node_id) : edges_on_start(new_node_id);

    // Work out what node and orientation we came from
    int64_t old_node_id = rank_to_id(current_side / 2);
    bool old_node_is_reverse = current_side % 2;

    // What edge are we following
    Edge edge_taken = make_edge(old_node_id, old_node_is_reverse, new_node_id, new_node_is_reverse);

    // Make sure we find it
    bool edge_found = false;

    for(auto& edge : edges) {
        // Look at every edge in order.

        if(edges_equivalent(edge, edge_taken)) {
            // If we found the edge we're taking, break.
            edge_found = true;
            break;
        }

        // Otherwise add in the threads on this edge to the offset

        // Get the orientation number for this edge (which is *not* the edge we
        // are following and so doesn't involve old_node_anything). We only
        // treat it as reverse if the reverse direction is the only direction we
        // can take to get here.
        int64_t edge_orientation_number = ((edge_rank_as_entity(edge) - 1) * 2) + arrive_by_reverse(edge, new_node_id, new_node_is_reverse);

        int64_t contribution = h_iv[edge_orientation_number];
#ifdef VERBOSE_DEBUG
        cerr << contribution << " (from prev edge " << edge.from() << (edge.from_start() ? "L" : "R") << "-" << edge.to() << (edge.to_end() ? "R" : "L") << " at " << edge_orientation_number << ") + ";
#endif
        new_visit_offset += contribution;
    }

    assert(edge_found);

    // What edge out of all the edges we can take are we taking?
    int64_t edge_taken_index = -1;

    // Look at the edges we could have taken next
    vector<Edge> edges_out = old_node_is_reverse ? edges_on_start(old_node_id) : edges_on_end(old_node_id);

    for(int64_t i = 0; i < edges_out.size(); i++) {
        if(edges_equivalent(edges_out[i], edge_taken)) {
            // i is the index of the edge we took, of the edges available to us.
            edge_taken_index = i;
            break;
        }
    }

    assert(edge_taken_index != -1);

    // Get the rank in B_s[] for our current side of our visit offset among
    // B_s[] entries pointing to the new node and add that in. Make sure to +2
    // to account for the nulls and separators.
    int64_t contribution = bs_rank(current_side, visit_offset, edge_taken_index + 2);
#ifdef VERBOSE_DEBUG
    cerr << contribution << " (via this edge) + ";
#endif
    new_visit_offset += contribution;

    // Get the number of threads starting at the new side and add that in.
    contribution = ts_iv[new_side];
#ifdef VERBOSE_DEBUG
    cerr << contribution << " (starting here) = ";
#endif
    new_visit_offset += contribution;
#ifdef VERBOSE_DEBUG
    cerr << new_visit_offset << endl;
#endif

    // Now we know where it actually ends up: after all the threads that start,
    // all the threads that come in via earlier edges, and all the previous
    // threads going there that come via this edge.
    return new_visit_offset;
}

void XG::insert_threads_into_dag(const vector<thread_t>& t) {

    auto emit_destinations = [&](int64_t node_id, bool is_reverse, vector<size_t> destinations) {
        // We have to take this destination vector and store it in whatever B_s
        // storage we are using.

        int64_t node_side = id_to_rank(node_id) * 2 + is_reverse;

        // Copy all the destinations into the succinct B_s storage
        bs_set(node_side, destinations);

        // Set the number of total visits to this side.
        h_iv[(node_rank_as_entity(node_id) - 1) * 2 + is_reverse] = destinations.size();

#ifdef VERBOSE_DEBUG
        cerr << "Found " << destinations.size() << " visits total to node " << node_id << (is_reverse ? "-" : "+") << endl;
#endif
    };

    auto emit_edge_traversal = [&](int64_t node_id, bool from_start, int64_t next_node_id, bool to_end) {
        // We have to store the fact that we traversed this edge in the specified direction in our succinct storage.

        // Find the edge as it actually appears in the graph.
        // TODO: make sure it exists.
        Edge canonical = canonicalize(make_edge(node_id, from_start, next_node_id, to_end));

        // We're departing along this edge, so our orientation cares about
        // whether we have to take the edge forward or backward when departing.
        int64_t edge_orientation_number = (edge_rank_as_entity(canonical) - 1) * 2 +
            depart_by_reverse(canonical, node_id, from_start);

#ifdef VERBOSE_DEBUG
        cerr << "We need to add 1 to the traversals of oriented edge " << edge_orientation_number << endl;
#endif

        // Increment the count for the edge
        h_iv[edge_orientation_number]++;
    };

    auto emit_thread_start = [&](int64_t node_id, bool is_reverse) {
        // Record that an (orientation of) a thread starts at this node in this
        // orientation. We have to update our thread start succinct data
        // structure.

        int64_t node_side = id_to_rank(node_id) * 2 + is_reverse;

        // Say we start one more thread on this side.
        ts_iv[node_side]++;

#ifdef VERBOSE_DEBUG
        cerr << "A thread starts at " << node_side << " for " <<  node_id << (is_reverse ? "-" : "+") << endl;
#endif

    };

    // We want to go through and insert running forward through the DAG, and
    // then again backward through the DAG.
    auto insert_in_direction = [&](bool insert_reverse) {

        // First sort out the thread numbers by the node they start at.
        // We know all the threads go the same direction through each node.
        map<int64_t, list<size_t>> thread_numbers_by_start_node;

        for(size_t i = 0; i < t.size(); i++) {
            if(t[i].size() > 0) {
                // Do we start with the first or last mapping in the thread?
                size_t thread_start = insert_reverse ? t[i].size() - 1 : 0;
                auto& mapping = t[i][thread_start];
                thread_numbers_by_start_node[mapping.node_id].push_back(i);

                // Say a thread starts here, going in the orientation determined
                // by how the node is visited and how we're traversing the path.
                emit_thread_start(mapping.node_id, mapping.is_reverse != insert_reverse);
            }
        }

        // We have this message-passing architecture, where we send groups of
        // threads along edges to destination nodes. This records, by edge rank of
        // the traversed edge (with 0 meaning starting there), the group of threads
        // coming in along that edge, and the offset in each thread that the visit
        // to the node is at. These are messages passed along the edge from the
        // earlier node to the later node (since we know threads follow a DAG).
        map<size_t, list<pair<size_t, size_t>>> edge_to_ordered_threads;

        for(size_t node_rank = (insert_reverse ? max_node_rank() : 1);
            node_rank != (insert_reverse ? 0 : max_node_rank() + 1);
            node_rank += (insert_reverse ? -1 : 1)) {
            // Then we start at the first node in the DAG

            int64_t node_id = rank_to_id(node_rank);

#ifdef VERBOSE_DEBUG
            if(node_id % 10000 == 1) {
                cerr << "Processing node " << node_id << endl;
            }
#endif

            // We order the thread visits starting there, and then all the threads
            // coming in from other places, ordered by edge traversed.
            // Stores a pair of thread number and mapping index in the thread.
            list<pair<size_t, size_t>> threads_visiting;

            if(thread_numbers_by_start_node.count(node_id)) {
                // Grab the threads starting here
                for(size_t thread_number : thread_numbers_by_start_node.at(node_id)) {
                    // For every thread that starts here, say it visits here
                    // with its first mapping (0 for forward inserts, last one
                    // for reverse inserts).
                    threads_visiting.emplace_back(thread_number, insert_reverse ? t[thread_number].size() - 1 : 0);
                }
                thread_numbers_by_start_node.erase(node_id);
            }


            for(Edge& in_edge : edges_of(node_id)) {
                // Look at all the edges on the node. Messages will only exist on
                // the incoming ones.
                auto edge_rank = edge_rank_as_entity(in_edge);
                if(edge_to_ordered_threads.count(edge_rank)) {
                    // We have messages coming along this edge on our start

                    // These threads come in next. Splice them in because they
                    // already have the right mapping indices.
                    threads_visiting.splice(threads_visiting.end(), edge_to_ordered_threads[edge_rank]);
                    edge_to_ordered_threads.erase(edge_rank);
                }
            }

            if(threads_visiting.empty()) {
                // Nothing visits here, so there's no cool succinct data structures
                // to generate.
                continue;
            }


            // Some threads visit here! Determine our orientation from the first
            // and assume it applies to all threads visiting us.
            auto& first_visit = threads_visiting.front();
            bool node_is_reverse = t[first_visit.first][first_visit.second].is_reverse;
            // When we're inserting threads backwards, we need to treat forward
            // nodes as reverse and visa versa, to leave the correct side.
            node_is_reverse = node_is_reverse != insert_reverse;

            // Now we have all the threads coming through this node, and we know
            // which way they are going.

            // Make a map from outgoing edge rank to the B_s array number (0 for
            // stop here, 1 reserved as a separator, and 2 through n corresponding
            // to outgoing edges in order) for this node's outgoing side.
            map<size_t, size_t> edge_rank_to_local_edge_number;
            auto outgoing_edges = node_is_reverse ? edges_on_start(node_id) : edges_on_end(node_id);
            for(size_t i = 0; i < outgoing_edges.size(); i++) {
                size_t edge_rank = edge_rank_as_entity(outgoing_edges[i]);
                edge_rank_to_local_edge_number[edge_rank] = i + 2;
            }

            // Make a vector we'll fill in with all the B array values (0 for stop,
            // 2 + edge number for outgoing edge)
            vector<size_t> destinations;

            for(auto& visit : threads_visiting) {
                // Now go through all the path visits, fill in the edge numbers (or 0
                // for stop) they go to, and stick visits to the next mappings in the
                // correct message lists.
                if(insert_reverse ? (visit.second != 0) : (visit.second + 1 < t[visit.first].size())) {
                    // This visit continues on from here

                    // Make a visit to the next mapping on the path
                    auto next_visit = visit;
                    next_visit.second += insert_reverse ? -1 : 1;

                    // Work out what node that is, and what orientation
                    auto& next_mapping = t[next_visit.first][next_visit.second];
                    int64_t next_node_id = next_mapping.node_id;
                    bool next_is_reverse = next_mapping.is_reverse != insert_reverse;

                    // Figure out the rank of the edge we need to take to get
                    // there. Note that we need to make sure we can handle going
                    // forward and backward over edges.
                    size_t next_edge_rank = edge_rank_as_entity(make_edge(node_id, node_is_reverse,
                        next_node_id, next_is_reverse));

                    // Look up what local edge number that edge gets and say we follow it.
                    destinations.push_back(edge_rank_to_local_edge_number.at(next_edge_rank));

                    // Send the new mapping along the edge after all the other ones
                    // we've sent along the edge
                    edge_to_ordered_threads[next_edge_rank].push_back(next_visit);

                    // Say we traverse an edge going from this node in this
                    // orientation to that node in that orientation.
                    emit_edge_traversal(node_id, node_is_reverse, next_node_id, next_is_reverse);

                } else {
                    // This visit ends here
#ifdef VERBOSE_DEBUG
                    if(insert_reverse) {
                        cerr << "A thread ends here because " << visit.second << " is 0 " << endl;
                    } else {
                        cerr << "A thread ends here because " << visit.second + 1 << " is >= " << t[visit.first].size() << endl;
                    }
#endif
                    destinations.push_back(BS_NULL);
                }
            }

            // Emit the destinations array for the node. Store it in whatever
            // sort of succinct storage we are using...
            // We need to send along the side (false for left, true for right)
            emit_destinations(node_id, node_is_reverse, destinations);

            // We repeat through all nodes until done.
        }

        // OK now we have gone through the whole set of everything and inserted
        // in this direction.
    };

    // Actually call the inserts
#ifdef VERBOSE_DEBUG
    cerr << "Inserting threads forwards..." << endl;
#endif
    insert_in_direction(false);
#ifdef VERBOSE_DEBUG
    cerr << "Inserting threads backwards..." << endl;
#endif
    insert_in_direction(true);

    // Actually build the B_s arrays for rank and select.
#ifdef VERBOSE_DEBUG
    cerr << "Creating final compressed array..." << endl;
#endif
    bs_bake();


}

void XG::insert_thread(const thread_t& t) {
    // We're going to insert this thread

    auto insert_thread_forward = [&](const thread_t& thread) {

#ifdef VERBOSE_DEBUG
        cerr << "Inserting thread with " << thread.size() << " mappings" << endl;
#endif
        // Where does the current visit fall on its node? On the first node we
        // arbitrarily decide to be first of all the threads starting there.
        // TODO: Make sure that we actually end up ordering starts based on path
        // name or something later.
        int64_t visit_offset = 0;
        for(size_t i = 0; i < thread.size(); i++) {
            // For each visit to a node...

            // What side are we visiting?
            int64_t node_id = thread[i].node_id;
            bool node_is_reverse = thread[i].is_reverse;
            int64_t node_side = id_to_rank(node_id) * 2 + node_is_reverse;

#ifdef VERBOSE_DEBUG
            cerr << "Visit side " << node_side << endl;
#endif

            // Where are we going next?

            if(i == thread.size() - 1) {
                // This is the last visit. Send us off to null

#ifdef VERBOSE_DEBUG
                cerr << "End the thread." << endl;
#endif
                // Stick a new entry in the B array at the place where it belongs.
                bs_insert(node_side, visit_offset, BS_NULL);
            } else {
                // This is not the last visit. Send us off to the next place, and update the count on the edge.

                // Work out where we're actually going next
                int64_t next_id = thread[i + 1].node_id;
                bool next_is_reverse = thread[i + 1].is_reverse;
                int64_t next_side = id_to_rank(next_id) * 2 + next_is_reverse;

                // What edge do we take to get there? We're going to search for
                // the actual edge articulated in the official orientation.
                Edge edge_wanted = make_edge(node_id, node_is_reverse, next_id, next_is_reverse);

                // And what is its index in the edges we can take?
                int64_t edge_taken_index = -1;

                // Look at the edges we could have taken
                vector<Edge> edges_out = node_is_reverse ? edges_on_start(node_id) : edges_on_end(node_id);
                for(int64_t j = 0; j < edges_out.size(); j++) {
                    if(edge_taken_index == -1 && edges_equivalent(edges_out[j], edge_wanted)) {
                        // i is the index of the edge we took, of the edges available to us.
                        edge_taken_index = j;
                    }
#ifdef VERBOSE_DEBUG
                    cerr << "Edge #" << j << ": "  << edges_out[j].from() << (edges_out[j].from_start() ? "L" : "R") << "-" << edges_out[j].to() << (edges_out[j].to_end() ? "R" : "L") << endl;

                    // Work out what its number is for the orientation it goes
                    // out in. We know we have this edge, so we just see if we
                    // have to depart in reveres and if so take the edge in
                    // reverse.
                    int64_t edge_orientation_number = (edge_rank_as_entity(edges_out[j]) - 1) * 2 + depart_by_reverse(edges_out[j], node_id, node_is_reverse);

                    cerr << "\tOrientation rank: " << edge_orientation_number << endl;
#endif
                }

                if(edge_taken_index == -1) {
                    cerr << "[xg] error: step " << i << " of thread: "
                        << edge_wanted.from() << (edge_wanted.from_start() ? "L" : "R") << "-"
                        << edge_wanted.to() << (edge_wanted.to_end() ? "R" : "L") << " does not exist" << endl;
                    cerr << "[xg] error: Possibilities: ";
                    for(Edge& edge_out : edges_out) {
                        cerr << edge_out.from() << (edge_out.from_start() ? "L" : "R") << "-"
                            << edge_out.to() << (edge_out.to_end() ? "R" : "L") << " ";
                    }
                    cerr << endl;
                    cerr << "[xg] error: On start: ";
                    for(Edge& edge_out : edges_on_start(node_id)) {
                        cerr << edge_out.from() << (edge_out.from_start() ? "L" : "R") << "-"
                            << edge_out.to() << (edge_out.to_end() ? "R" : "L") << " ";
                    }
                    cerr << endl;
                    cerr << "[xg] error: On end: ";
                    for(Edge& edge_out : edges_on_end(node_id)) {
                        cerr << edge_out.from() << (edge_out.from_start() ? "L" : "R") << "-"
                            << edge_out.to() << (edge_out.to_end() ? "R" : "L") << " ";
                    }
                    cerr << endl;
                    cerr << "[xg] error: Both: ";
                    for(Edge& edge_out : edges_of(node_id)) {
                        cerr << edge_out.from() << (edge_out.from_start() ? "L" : "R") << "-"
                            << edge_out.to() << (edge_out.to_end() ? "R" : "L") << " ";
                    }
                    cerr << endl;
                    cerr << "[xg] error: has_edge: " << has_edge(edge_wanted.from(), edge_wanted.from_start(), edge_wanted.to(), edge_wanted.to_end()) << endl;
                    assert(false);
                }

                assert(edge_taken_index != -1);

#ifdef VERBOSE_DEBUG
                cerr << "Proceed to " << next_side << " via edge #" << edge_taken_index << "/" << edges_out.size() << endl;
#endif

                // Make a nice reference to the edge we're taking in its real orientation.
                auto& edge_taken = edges_out[edge_taken_index];

                // Stick a new entry in the B array at the place where it belongs.
                // Make sure to +2 to leave room in the number space for the separators and null destinations.
                bs_insert(node_side, visit_offset, edge_taken_index + 2);

                // Update the usage count for the edge going form here to the next node
                // Make sure that edge storage direction is correct.
                // Which orientation of an edge are we crossing?
                int64_t edge_orientation_number = (edge_rank_as_entity(edge_taken) - 1) * 2 + depart_by_reverse(edge_taken, node_id, node_is_reverse);

#ifdef VERBOSE_DEBUG
                cerr << "We need to add 1 to the traversals of oriented edge " << edge_orientation_number << endl;
#endif

                // Increment the count for the edge
                h_iv[edge_orientation_number]++;

#ifdef VERBOSE_DEBUG
                cerr << "h = [";
                for(int64_t k = 0; k < h_iv.size(); k++) {
                    cerr << h_iv[k] << ", ";
                }
                cerr << "]" << endl;
#endif

                // Now where do we go to on the next visit?
                visit_offset = where_to(node_side, visit_offset, next_side);

#ifdef VERBOSE_DEBUG

                cerr << "Offset " << visit_offset << " at side " << next_side << endl;
#endif

            }

#ifdef VERBOSE_DEBUG

            cerr << "Node " << node_id << " orientation " << node_is_reverse <<
                " has rank " <<
                ((node_rank_as_entity(node_id) - 1) * 2 + node_is_reverse) <<
                " of " << h_iv.size() << endl;
#endif

            // Increment the usage count of the node in this orientation
            h_iv[(node_rank_as_entity(node_id) - 1) * 2 + node_is_reverse]++;

            if(i == 0) {
                // The thread starts here
                ts_iv[node_side]++;
            }
        }

    };

    // We need a simple reverse that works only for perfect match paths
    auto simple_reverse = [&](const thread_t& thread) {
        // Make a reversed version
        thread_t reversed;

        // TODO: give it a reversed name or something

        for(size_t i = thread.size(); i != (size_t) -1; i--) {
            // Copy the mappings from back to front, flipping the is_reverse on their positions.
            ThreadMapping reversing = thread[thread.size() - 1 - i];
            reversing.is_reverse = !reversing.is_reverse;
            reversed.push_back(reversing);
        }

        return reversed;
    };

    // Insert forward
    insert_thread_forward(t);

    // Insert reverse
    insert_thread_forward(simple_reverse(t));

    // TODO: name annotation

}

auto XG::extract_threads() const -> list<thread_t> {

    // Fill in a lsut of paths found
    list<thread_t> found;

#ifdef VERBOSE_DEBUG
    cerr << "Extracting threads" << endl;
#endif

    for(int64_t i = 1; i < ts_iv.size(); i++) {
        // For each real side

#ifdef VERBOSE_DEBUG
        cerr << ts_iv[i] << " threads start at side " << i << endl;
#endif

        // For each side
        if(ts_iv[i] == 0) {
            // Skip it if no threads start at it
            continue;
        }

        for(int64_t j = 0; j < ts_iv[i]; j++) {
            // For every thread starting there

#ifdef debug
            cerr << "Extracting thread " << j << endl;
#endif

            // make a new thread
            thread_t path;

            // Start the side at i and the offset at j
            int64_t side = i;
            int64_t offset = j;

            while(true) {

                // Unpack the side into a node traversal
                ThreadMapping m = {rank_to_id(side / 2), (bool) (side % 2)};

                // Add the mapping to the thread
                path.push_back(m);

#ifdef VERBOSE_DEBUG
                cerr << "At side " << side << endl;

#endif
                // Work out where we go

                // What edge of the available edges do we take?
                int64_t edge_index = bs_get(side, offset);

                // If we find a separator, we're very broken.
                assert(edge_index != BS_SEPARATOR);

                if(edge_index == BS_NULL) {
                    // Path ends here.
                    break;
                } else {
                    // Convert to an actual edge index
                    edge_index -= 2;
                }

#ifdef VERBOSE_DEBUG
                cerr << "Taking edge #" << edge_index << " from " << side << endl;
#endif

                // We also should not have negative edges.
                assert(edge_index >= 0);

                // Look at the edges we could have taken next
                vector<Edge> edges_out = side % 2 ? edges_on_start(rank_to_id(side / 2)) : edges_on_end(rank_to_id(side / 2));

                assert(edge_index < edges_out.size());

                Edge& taken = edges_out[edge_index];

#ifdef VERBOSE_DEBUG
                cerr << edges_out.size() << " edges possible." << endl;
#endif
                // Follow the edge
                int64_t other_node = taken.from() == rank_to_id(side / 2) ? taken.to() : taken.from();
                bool other_orientation = (side % 2) != taken.from_start() != taken.to_end();

                // Get the side
                int64_t other_side = id_to_rank(other_node) * 2 + other_orientation;

#ifdef VERBOSE_DEBUG
                cerr << "Go to side " << other_side << endl;
#endif

                // Go there with where_to
                offset = where_to(side, offset, other_side);
                side = other_side;

            }

            found.push_back(path);

        }
    }

    return found;
}

XG::destination_t XG::bs_get(int64_t side, int64_t offset) const {
#if GPBWT_MODE == MODE_SDSL
    if(!bs_arrays.empty()) {
        // We still have per-side arrays
        return bs_arrays.at(side - 2)[offset];
    } else {
        // We have a single big array
#ifdef VERBOSE_DEBUG
        cerr << "Range " << side << " has its separator at " << bs_single_array.select(side, BS_SEPARATOR) << endl;
        cerr << "Offset " << offset << " puts us at " << bs_single_array.select(side, BS_SEPARATOR) + 1 + offset << endl;
#endif
        return bs_single_array[bs_single_array.select(side, BS_SEPARATOR) + 1 + offset];
    }
#elif GPBWT_MODE == MODE_DYNAMIC
    // Start after the separator for the side and go offset from there.

    auto& bs_single_array = const_cast<rank_select_int_vector&>(this->bs_single_array);

    return bs_single_array.at(bs_single_array.select(side - 2, BS_SEPARATOR) + 1 + offset);
#endif

}

size_t XG::bs_rank(int64_t side, int64_t offset, destination_t value) const {
#if GPBWT_MODE == MODE_SDSL
    if(!bs_arrays.empty()) {
        throw runtime_error("No rank support until bs_bake() is called!");
    } else {
        size_t range_start = bs_single_array.select(side, BS_SEPARATOR) + 1;
        return bs_single_array.rank(range_start + offset, value) - bs_single_array.rank(range_start, value);
    }
#elif GPBWT_MODE == MODE_DYNAMIC

    auto& bs_single_array = const_cast<rank_select_int_vector&>(this->bs_single_array);

    // Where does the B_s[] range for the side we're interested in start?
    int64_t bs_start = bs_single_array.select(side - 2, BS_SEPARATOR) + 1;

    // Get the rank difference between the start and the start plus the offset.
    return bs_single_array.rank(bs_start + offset, value) - bs_single_array.rank(bs_start, value);
#endif
}

void XG::bs_set(int64_t side, vector<destination_t> new_array) {
#if GPBWT_MODE == MODE_SDSL
    // We always know bs_arrays will be big enough.

    // Turn the new array into a string of bytes.
    // TODO: none of the destinations can be 255 or greater!
    bs_arrays.at(side - 2) = string(new_array.size(), 0);
    copy(new_array.begin(), new_array.end(), bs_arrays.at(side - 2).begin());

#ifdef VERBOSE_DEBUG
    cerr << "B_s for " << side << ": ";
    for(auto entry : bs_arrays.at(side - 2)) {
        cerr << to_string(entry);
    }
    cerr << endl;
#endif
#elif GPBWT_MODE == MODE_DYNAMIC
    // Where does the block we want start?
    size_t this_range_start = bs_single_array.select(side - 2, BS_SEPARATOR) + 1;

    // Where is the first spot not in the range for this side?
    int64_t this_range_past_end = (side - 2 == bs_single_array.rank(bs_single_array.size(), BS_SEPARATOR) - 1 ?
        bs_single_array.size() : bs_single_array.select(side - 2 + 1, BS_SEPARATOR));

    if(this_range_start != this_range_past_end) {
        // We can't overwrite! Just explode.
        throw runtime_error("B_s overwrite not supported");
    }

    size_t bs_insert_index = this_range_start;

    for(auto destination : new_array) {
        // Blit everything into the B_s array
        bs_single_array.insert(bs_insert_index, destination);
        bs_insert_index++;
    }
#endif
}

void XG::bs_insert(int64_t side, int64_t offset, destination_t value) {
#if GPBWT_MODE == MODE_SDSL
    // This is a pretty slow insert. Use set instead.

    auto& array_to_expand = bs_arrays.at(side - 2);

    // Stick one copy of the new entry in at the right position.
    array_to_expand.insert(offset, 1, value);
#elif GPBWT_MODE == MODE_DYNAMIC
     // Find the place to put it in the correct side's B_s and insert
     bs_single_array.insert(bs_single_array.select(side - 2, BS_SEPARATOR) + 1 + offset, value);
#endif
}

void XG::bs_bake() {
#if GPBWT_MODE == MODE_SDSL
    // First pass: determine required size
    size_t total_visits = 1;
    for(auto& bs_array : bs_arrays) {
        total_visits += 1; // For the separator
        total_visits += bs_array.size();
    }

#ifdef VERBOSE_DEBUG
    cerr << "Allocating giant B_s array of " << total_visits << " bytes..." << endl;
#endif
    // Move over to a single array which is big enough to start out with.
    string all_bs_arrays(total_visits, 0);

    // Where are we writing to?
    size_t pos = 0;

    // Start with a separator for sides 0 and 1.
    // We don't start at run 0 because we can't select(0, BS_SEPARATOR).
    all_bs_arrays[pos++] = BS_SEPARATOR;

#ifdef VERBOSE_DEBUG
    cerr << "Baking " << bs_arrays.size() << " sides' arrays..." << endl;
#endif

    for(auto& bs_array : bs_arrays) {
        // Stick everything together with a separator at the front of every
        // range.
        all_bs_arrays[pos++] = BS_SEPARATOR;
        for(size_t i = 0; i < bs_array.size(); i++) {
            all_bs_arrays[pos++] = bs_array[i];
        }
        bs_array.clear();
    }

#ifdef VERBOSE_DEBUG
    cerr << "B_s: ";
    for(auto entry : all_bs_arrays) {
        cerr << to_string(entry);
    }
    cerr << endl;
#endif

    // Rebuild based on the entire concatenated string.
    construct_im(bs_single_array, all_bs_arrays, 1);

    bs_arrays.clear();
#endif
}

size_t XG::count_matches(const thread_t& t) const {
    // This is just a really simple wrapper that does a single extend
    ThreadSearchState state;
    extend_search(state, t);
    return state.count();
}

size_t XG::count_matches(const Path& t) const {
    // We assume the path is a thread and convert.
    thread_t thread;
    for(size_t i = 0; i < t.mapping_size(); i++) {
        // Convert the mapping
        ThreadMapping m = {t.mapping(i).position().node_id(), t.mapping(i).position().is_reverse()};

        thread.push_back(m);
    }

    // Count matches to the converted thread
    return count_matches(thread);
}

void XG::extend_search(ThreadSearchState& state, const thread_t& t) const {

#ifdef VERBOSE_DEBUG
    cerr << "Looking for path: ";
    for(int64_t i = 0; i < t.size(); i++) {
        // For each item in the path
        const ThreadMapping& mapping = t[i];
        int64_t next_id = mapping.node_id;
        bool next_is_reverse = mapping.is_reverse;
        int64_t next_side = id_to_rank(next_id) * 2 + next_is_reverse;
        cerr << next_side << "; ";
    }
    cerr << endl;
#endif


    for(int64_t i = 0; i < t.size(); i++) {
        // For each item in the path
        const ThreadMapping& mapping = t[i];

        if(state.is_empty()) {
            // Don't bother trying to extend empty things.

#ifdef VERBOSE_DEBUG
            cerr << "Nothing selected!" << endl;
#endif

            break;
        }

        // TODO: make this mapping to side thing a function
        int64_t next_id = mapping.node_id;
        bool next_is_reverse = mapping.is_reverse;
        int64_t next_side = id_to_rank(next_id) * 2 + next_is_reverse;

#ifdef VERBOSE_DEBUG
        cerr << "Extend mapping to " << state.current_side << " range " << state.range_start << " to " << state.range_end << " with " << next_side << endl;
#endif

        if(state.current_side == 0) {
            // If the state is a start state, just select the whole node using
            // the node usage count in this orientation. TODO: orientation not
            // really important unless we're going to search during a path
            // addition.
            state.range_start = 0;
            state.range_end = h_iv[(node_rank_as_entity(next_id) - 1) * 2 + next_is_reverse];

#ifdef VERBOSE_DEBUG
            cerr << "\tFound " << state.range_end << " threads present here." << endl;

            int64_t here = (node_rank_as_entity(next_id) - 1) * 2 + next_is_reverse;
            cerr << here << endl;
            for(int64_t i = here - 5; i < here + 5; i++) {
                if(i >= 0) {
                    cerr << "\t\t" << (i == here ? "*" : " ") << "h_iv[" << i << "] = " << h_iv[i] << endl;
                }
            }

#endif

        } else {
            // Else, look at where the path goes to and apply the where_to function to shrink the range down.
            state.range_start = where_to(state.current_side, state.range_start, next_side);
            state.range_end = where_to(state.current_side, state.range_end, next_side);

#ifdef VERBOSE_DEBUG
            cerr << "\tFound " << state.range_start << " to " << state.range_end << " threads continuing through." << endl;
#endif

        }

        // Update the side that the state is on
        state.current_side = next_side;
    }
}

size_t serialize(XG::rank_select_int_vector& to_serialize, ostream& out,
    sdsl::structure_tree_node* parent, const std::string name) {
#if GPBWT_MODE == MODE_SDSL
    // Just delegate to the SDSL code
    return to_serialize.serialize(out, parent, name);

#elif GPBWT_MODE == MODE_DYNAMIC
    // We need to check to make sure we're actually writing the correct numbers of bytes.
    size_t start = out.tellp();

    // We just use the DYNAMIC serialization.
    size_t written = to_serialize.serialize(out);

    // TODO: when https://github.com/nicolaprezza/DYNAMIC/issues/4 is closed,
    // trust the sizes that DYNAMIC reports. For now, second-guess it and just
    // look at how far the stream has actually moved.
    written = (size_t) out.tellp() - start;

    // And then do the structure tree stuff
    sdsl::structure_tree_node* child = structure_tree::add_child(parent, name, sdsl::util::class_name(to_serialize));
    sdsl::structure_tree::add_size(child, written);

    return written;
#endif
}

void deserialize(XG::rank_select_int_vector& target, istream& in) {
#if GPBWT_MODE == MODE_SDSL
    // We just load using the SDSL deserialization code
    target.load(in);
#elif GPBWT_MODE == MODE_DYNAMIC
    // The DYNAMIC code has the same API
    target.load(in);
#endif
}

bool edges_equivalent(const Edge& e1, const Edge& e2) {
    return ((e1.from() == e2.from() && e1.to() == e2.to() && e1.from_start() == e2.from_start() && e1.to_end() == e2.to_end()) ||
        (e1.from() == e2.to() && e1.to() == e2.from() && e1.from_start() == !e2.to_end() && e1.to_end() == !e2.from_start()));
}

bool relative_orientation(const Edge& e1, const Edge& e2) {
    assert(edges_equivalent(e1, e2));

    // Just use the reverse-equivalence check from edges_equivalent.
    return e1.from() == e2.to() && e1.to() == e2.from() && e1.from_start() == !e2.to_end() && e1.to_end() == !e2.from_start();
}

bool arrive_by_reverse(const Edge& e, int64_t node_id, bool node_is_reverse) {
    if(e.to() == node_id && (node_is_reverse == e.to_end())) {
        // We can follow the edge forwards and arrive at the correct side of the node
        return false;
    } else if(e.to() == e.from() && e.from_start() != e.to_end()) {
        // Reversing self loop
        return false;
    }
    // Otherwise, since we know the edge goes to the node, we have to take it backward.
    return true;
}

bool depart_by_reverse(const Edge& e, int64_t node_id, bool node_is_reverse) {
    if(e.from() == node_id && (node_is_reverse == e.from_start())) {
        // We can follow the edge forwards and arrive at the correct side of the node
        return false;
    } else if(e.to() == e.from() && e.from_start() != e.to_end()) {
        // Reversing self loop
        return false;
    }
    // Otherwise, since we know the edge goes to the node, we have to take it backward.
    return true;
}

Edge make_edge(int64_t from, bool from_start, int64_t to, bool to_end) {
    Edge e;
    e.set_from(from);
    e.set_to(to);
    e.set_from_start(from_start);
    e.set_to_end(to_end);

    return e;
}

char reverse_complement(const char& c) {
    switch (c) {
        case 'A': return 'T'; break;
        case 'T': return 'A'; break;
        case 'G': return 'C'; break;
        case 'C': return 'G'; break;
        case 'N': return 'N'; break;
        // Handle the GCSA2 start/stop characters.
        case '#': return '$'; break;
        case '$': return '#'; break;
        default: return 'N';
    }
}

string reverse_complement(const string& seq) {
    string rc;
    rc.assign(seq.rbegin(), seq.rend());
    for (auto& c : rc) {
        switch (c) {
        case 'A': c = 'T'; break;
        case 'T': c = 'A'; break;
        case 'G': c = 'C'; break;
        case 'C': c = 'G'; break;
        case 'N': c = 'N'; break;
        // Handle the GCSA2 start/stop characters.
        case '#': c = '$'; break;
        case '$': c = '#'; break;
        default: break;
        }
    }
    return rc;
}

void extract_pos(const string& pos_str, int64_t& id, bool& is_rev, size_t& off) {
    // format is id:off for forward, and id:-off for reverse
    // find the colon
    auto s = pos_str.find(":");
    assert(s != string::npos);
    id = stol(pos_str.substr(0, s));
    auto r = pos_str.find("-", s);
    if (r == string::npos) {
        is_rev = false;
        off = stoi(pos_str.substr(s+1, pos_str.size()));
    } else {
        is_rev = true;
        off = stoi(pos_str.substr(r+1, pos_str.size()));
    }
}

void extract_pos_substr(const string& pos_str, int64_t& id, bool& is_rev, size_t& off, size_t& len) {
    // format is id:off:len on forward and id:-off:len on reverse
    auto s = pos_str.find(":");
    assert(s != string::npos);
    id = stol(pos_str.substr(0, s));
    auto r = pos_str.find("-", s);
    if (r == string::npos) {
        is_rev = false;
        // find second colon
        auto t = pos_str.find(":", s+1);
        assert(t != string::npos);
        off = stoi(pos_str.substr(s+1, t-s));
        len = stoi(pos_str.substr(t+1, pos_str.size()));
    } else {
        is_rev = true;
        auto t = pos_str.find(":", r+1);
        assert(t != string::npos);
        off = stoi(pos_str.substr(r+1, t-r+1));
        len = stoi(pos_str.substr(t+1, pos_str.size()));
    }
}

}
