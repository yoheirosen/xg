#ifndef SUCCINCT_GRAPH_SG_HPP
#define SUCCINCT_GRAPH_SG_HPP

#include <iostream>
#include <fstream>
#include <map>
#include <omp.h>
#include "cpp/vg.pb.h"
#include "sdsl/bit_vectors.hpp"
#include "sdsl/enc_vector.hpp"
#include "sdsl/dac_vector.hpp"
#include "sdsl/vlc_vector.hpp"
#include "sdsl/wavelet_trees.hpp"
//#include "sdsl/csa_bitcompressed.hpp"
#include "sdsl/csa_wt.hpp"
#include "sdsl/suffix_arrays.hpp"


namespace xg {

using namespace std;
using namespace sdsl;
using namespace vg;

class Traversal {
public:
    int64_t id;
    bool rev;
    Traversal(int64_t i, bool r) : id(i), rev(r) { }
};

class XGPath;
typedef pair<int64_t, bool> Side;

class XG {
public:
    
    XG(void) : start_marker('#'),
               end_marker('$'),
               seq_length(0),
               node_count(0),
               edge_count(0),
               path_count(0) { }
    ~XG(void) { }
    XG(istream& in);
    void from_vg(istream& in, bool validate_graph = false, bool print_graph = false);
    void load(istream& in);
    size_t serialize(std::ostream& out,
                     sdsl::structure_tree_node* v = NULL,
                     std::string name = "");
    size_t seq_length;
    size_t node_count;
    size_t edge_count;
    size_t path_count;

    size_t id_to_rank(int64_t id) const;
    int64_t rank_to_id(size_t rank) const;
    size_t max_node_rank(void) const;
    Node node(int64_t id) const; // gets node sequence
    string node_sequence(int64_t id) const;
    vector<Edge> edges_of(int64_t id) const;
    vector<Edge> edges_to(int64_t id) const;
    vector<Edge> edges_from(int64_t id) const;
    vector<Edge> edges_on_start(int64_t id) const;
    vector<Edge> edges_on_end(int64_t id) const;
    size_t node_rank_as_entity(int64_t id) const;
    size_t edge_rank_as_entity(int64_t id1, int64_t id2) const;
    bool entity_is_node(size_t rank) const;
    size_t entity_rank_as_node_rank(size_t rank) const;
    bool has_edge(int64_t id1, int64_t id2) const;

    Path path(const string& name) const;
    size_t path_rank(const string& name) const;
    size_t max_path_rank(void) const;
    string path_name(size_t rank) const;
    vector<size_t> paths_of_entity(size_t rank) const;
    vector<size_t> paths_of_node(int64_t id) const;
    vector<size_t> paths_of_edge(int64_t id1, int64_t id2) const;
    map<string, Mapping> node_mappings(int64_t id) const;
    bool path_contains_node(const string& name, int64_t id) const;
    bool path_contains_edge(const string& name, int64_t id1, int64_t id2) const;
    bool path_contains_entity(const string& name, size_t rank) const;
    void add_paths_to_graph(map<int64_t, Node*>& nodes, Graph& g) const;
    size_t node_occs_in_path(int64_t id, const string& name) const;
    size_t node_position_in_path(int64_t id, const string& name) const;
    int64_t node_at_path_position(const string& name, size_t pos) const;
    size_t path_length(const string& name) const;

    void neighborhood(int64_t id, size_t steps, Graph& g) const;
    //void for_path_range(string& name, int64_t start, int64_t stop, function<void(Node)> lambda);
    void get_path_range(string& name, int64_t start, int64_t stop, Graph& g) const;
    void expand_context(Graph& g, size_t steps) const;
    void get_connected_nodes(Graph& g) const;
    void get_id_range(int64_t id1, int64_t id2, Graph& g) const;

    
    char start_marker;
    char end_marker;
    
private:

    // sequence/integer vector
    int_vector<> s_iv;
    // node starts in sequence, provides id schema
    // rank_1(i) = id
    // select_1(id) = i
    bit_vector s_bv; // node positions in siv
    rank_support_v<1> s_bv_rank;
    bit_vector::select_1_type s_bv_select;
    // compressed version, unused...
    rrr_vector<> s_cbv;
    rrr_vector<>::rank_1_type s_cbv_rank;
    rrr_vector<>::select_1_type s_cbv_select;

    // maintain old ids from input, ranked as in s_iv and s_bv
    int_vector<> i_iv;

    // maintain forward links
    int_vector<> f_iv;
    bit_vector f_bv;
    rank_support_v<1> f_bv_rank;
    bit_vector::select_1_type f_bv_select;
    bit_vector f_from_start_bv;
    bit_vector f_to_end_bv;
    sd_vector<> f_from_start_cbv;
    sd_vector<> f_to_end_cbv;

    // and the same data in the reverse direction
    int_vector<> t_iv;
    bit_vector t_bv;
    rank_support_v<1> t_bv_rank;
    bit_vector::select_1_type t_bv_select;
    // these bit vectors are only used during construction
    // perhaps they should be moved?
    bit_vector t_from_start_bv;
    bit_vector t_to_end_bv;
    // used at runtime
    sd_vector<> t_from_start_cbv;
    sd_vector<> t_to_end_cbv;

    // edge table, allows o(1) determination of edge existence
    int_vector<> e_iv;

    //csa_wt<> e_csa;
    csa_sada<> e_csa;

    // allows lookups of id->rank mapping
    wt_int<> i_wt;

    // paths: serialized as bitvectors over nodes and edges
    int_vector<> pn_iv; // path names
    csa_wt<> pn_csa; // path name compressed suffix array
    bit_vector pn_bv;  // path name starts in uncompressed version of csa
    rank_support_v<1> pn_bv_rank;
    bit_vector::select_1_type pn_bv_select;
    int_vector<> pi_iv; // path ids by rank in the path names

    // probably these should get compressed, for when we have whole genomes with many chromosomes
    // the growth in required memory is quadratic but the stored matrix is sparse
    vector<XGPath*> paths; // path entity membership

    // entity->path membership
    int_vector<> ep_iv;
    bit_vector ep_bv; // entity delimiters in ep_iv
    rank_support_v<1> ep_bv_rank;
    bit_vector::select_1_type ep_bv_select;
};

class XGPath {
public:
    XGPath(void) : member_count(0) { }
    ~XGPath(void) { }
    XGPath(const string& path_name,
           const vector<Traversal>& path,
           size_t entity_count,
           XG& graph,
           map<int64_t, string>& node_label);
    string name;
    size_t member_count;
    sd_vector<> members;
    wt_int<> ids;
    sd_vector<> directions; // forward or backward through nodes
    int_vector<> positions;
    bit_vector offsets;
    rank_support_v<1> offsets_rank;
    bit_vector::select_1_type offsets_select;
    void load(istream& in);
    size_t serialize(std::ostream& out,
                     sdsl::structure_tree_node* v = NULL,
                     std::string name = "");
};


Mapping new_mapping(const string& name, int64_t id);
void parse_region(const string& target, string& name, int64_t& start, int64_t& end);
void to_text(ostream& out, Graph& graph);

}

#endif
