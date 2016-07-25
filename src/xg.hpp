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
#include "hash_map_set.hpp"

// We can have DYNAMIC or SDSL-based gPBWTs
#define MODE_DYNAMIC 1
#define MODE_SDSL 2

#define GPBWT_MODE MODE_SDSL

#if GPBWT_MODE == MODE_DYNAMIC
#include "dynamic.hpp"
#endif

namespace xg {

using namespace std;
using namespace sdsl;
using namespace vg;

class XGPath;
//typedef pair<int64_t, bool> Side;
typedef int64_t id_t; // generic id type
// node sides
typedef int64_t side_t;
id_t side_id(const side_t& side);
bool side_is_end(const side_t& side);
side_t make_side(id_t id, bool is_end);
// node traversals
typedef pair<int64_t, int32_t> trav_t; // meant to encode pos+side or pos+strand
id_t trav_id(const trav_t& trav);
bool trav_is_rev(const trav_t& trav);
int32_t trav_rank(const trav_t& trav);
trav_t make_trav(id_t id, bool is_end, int32_t rank);


class XG {
public:

    XG(void) : start_marker('#'),
               end_marker('$'),
               seq_length(0),
               node_count(0),
               edge_count(0),
               path_count(0) { }
    ~XG(void);
    XG(istream& in);
    XG(Graph& graph);
    XG(function<void(function<void(Graph&)>)> get_chunks);
    void from_stream(istream& in, bool validate_graph = false,
        bool print_graph = false, bool store_threads = false,
        bool is_sorted_dag = false);
    void from_graph(Graph& graph, bool validate_graph = false,
        bool print_graph = false, bool store_threads = false,
        bool is_sorted_dag = false);
    // Load the graph by calling a function that calls us back with graph chunks.
    // The function passed in here is responsible for looping.
    // If is_sorted_dag is true and store_threads is true, we store the threads
    // with an algorithm that only works on topologically sorted DAGs, but which
    // is faster.
    void from_callback(function<void(function<void(Graph&)>)> get_chunks,
        bool validate_graph = false, bool print_graph = false,
        bool store_threads = false, bool is_sorted_dag = false);
    void build(map<id_t, string>& node_label,
               map<side_t, set<side_t> >& from_to,
               map<side_t, set<side_t> >& to_from,
               map<string, vector<trav_t> >& path_nodes,
               bool validate_graph,
               bool print_graph,
               bool store_threads,
               bool is_sorted_dag);
    void load(istream& in);
    size_t serialize(std::ostream& out,
                     sdsl::structure_tree_node* v = NULL,
                     std::string name = "");
    size_t seq_length;
    size_t node_count;
    size_t edge_count;
    size_t path_count;

    // We need the w function, which we call the "where_to" function. It tells
    // you, from a given visit at a given side, what visit offset if you go to
    // another side.
    int64_t where_to(int64_t current_side, int64_t visit_offset, int64_t new_side) const;

    size_t id_to_rank(int64_t id) const;
    int64_t rank_to_id(size_t rank) const;
    size_t max_node_rank(void) const;
    int64_t node_at_seq_pos(size_t pos) const;
    size_t node_start(int64_t id) const;
    Node node(int64_t id) const; // gets node sequence
    string node_sequence(int64_t id) const;
    size_t node_length(int64_t id) const;
    char pos_char(int64_t id, bool is_rev, size_t off) const; // character at position
    string pos_substr(int64_t id, bool is_rev, size_t off, size_t len = 0) const; // substring in range
    vector<Edge> edges_of(int64_t id) const;
    vector<Edge> edges_to(int64_t id) const;
    vector<Edge> edges_from(int64_t id) const;
    vector<Edge> edges_on_start(int64_t id) const;
    vector<Edge> edges_on_end(int64_t id) const;
    size_t node_rank_as_entity(int64_t id) const;
    size_t edge_rank_as_entity(int64_t id1, bool from_start, int64_t id2, bool to_end) const;
    // Supports the edge articulated in any orientation. Edge must exist.
    size_t edge_rank_as_entity(const Edge& edge) const;
    // Given an edge which is in the graph in some orientation, return the edge
    // oriented as it actually appears.
    Edge canonicalize(const Edge& edge);
    bool entity_is_node(size_t rank) const;
    size_t entity_rank_as_node_rank(size_t rank) const;
    bool has_edge(int64_t id1, bool is_start, int64_t id2, bool is_end) const;

    Path path(const string& name) const;
    // Returns the rank of the path with the given name, or 0 if no such path
    // exists.
    size_t path_rank(const string& name) const;
    // Returns the maxiumum rank of any existing path. A path does exist at this
    // rank.
    size_t max_path_rank(void) const;
    // Get the name of the path at the given rank. Ranks begin at 1.
    string path_name(size_t rank) const;
    vector<size_t> paths_of_entity(size_t rank) const;
    vector<size_t> paths_of_node(int64_t id) const;
    vector<size_t> paths_of_edge(int64_t id1, bool from_start, int64_t id2, bool to_end) const;
    map<string, vector<Mapping>> node_mappings(int64_t id) const;
    bool path_contains_node(const string& name, int64_t id) const;
    bool path_contains_edge(const string& name,
                            int64_t id1, bool from_start,
                            int64_t id2, bool to_end) const;
    bool path_contains_entity(const string& name, size_t rank) const;
    void add_paths_to_graph(map<int64_t, Node*>& nodes, Graph& g) const;
    size_t node_occs_in_path(int64_t id, const string& name) const;
    size_t node_occs_in_path(int64_t id, size_t rank) const;
    vector<size_t> node_ranks_in_path(int64_t id, const string& name) const;
    vector<size_t> node_ranks_in_path(int64_t id, size_t rank) const;
    vector<size_t> node_positions_in_path(int64_t id, const string& name) const;
    vector<size_t> node_positions_in_path(int64_t id, size_t rank) const;
    map<string, vector<size_t> > node_positions_in_paths(int64_t id) const;
    int64_t node_at_path_position(const string& name, size_t pos) const;
    Mapping mapping_at_path_position(const string& name, size_t pos) const;
    size_t path_length(const string& name) const;
    // if node is on path, return it.  otherwise, return next node (in id space)
    // that is on path.  if none exists, return 0
    int64_t next_path_node_by_id(size_t path_rank, int64_t id) const;
    // if node is on path, return it.  otherwise, return previous node (in id space)
    // that is on path.  if none exists, return 0
    int64_t prev_path_node_by_id(size_t path_rank, int64_t id) const;
    // estimate distance (in bp) between two nodes along a path.
    // if a nodes isn't on the path, the nearest node on the path (using id space)
    // is used as a proxy.
    int64_t approx_path_distance(const string& name, int64_t id1, int64_t id2) const;
    // like above, but find minumum over list of paths.  if names is empty, do all paths
    int64_t min_approx_path_distance(const vector<string>& names, int64_t id1, int64_t id2) const;


    // use_steps flag toggles whether dist refers to steps or length in base pairs
    void neighborhood(int64_t id, size_t dist, Graph& g, bool use_steps = true) const;
    //void for_path_range(string& name, int64_t start, int64_t stop, function<void(Node)> lambda);
    void get_path_range(string& name, int64_t start, int64_t stop, Graph& g) const;
    // basic method to query regions of the graph
    // add_paths flag allows turning off the (potentially costly, and thread-locking) addition of paths
    // when these are not necessary
    // use_steps flag toggles whether dist refers to steps or length in base pairs
    void expand_context(Graph& g, size_t dist, bool add_paths = true, bool use_steps = true) const;
    // expand by steps (original and default)
    void expand_context_by_steps(Graph& g, size_t steps, bool add_paths = true) const;
    // expand by length
    void expand_context_by_length(Graph& g, size_t length, bool add_paths = true) const;
    void get_connected_nodes(Graph& g) const;
    void get_id_range(int64_t id1, int64_t id2, Graph& g) const;
    // walk forward in id space, collecting nodes, until at least length bases covered
    // (or end of graph reached).  if forward is false, go backwards...
    void get_id_range_by_length(int64_t id1, int64_t length, Graph& g, bool forward) const;

    // gPBWT interface

#if GPBWT_MODE == MODE_SDSL
    // We keep our strings in instances of this cool wavelet tree.
    using rank_select_int_vector = sdsl::wt_huff<sdsl::rrr_vector<>>;
#elif GPBWT_MODE == MODE_DYNAMIC
    using rank_select_int_vector = dyn::rle_str;
#endif


    // We define a thread visit that's much smaller than a Protobuf Mapping.
    struct ThreadMapping {
        int64_t node_id;
        bool is_reverse;
    };

    int64_t node_height(ThreadMapping node) const;
    int64_t threads_starting_at_node(ThreadMapping node) const;

    // We define a thread as just a vector of these things, instead of a bulky
    // Path.
    using thread_t = vector<ThreadMapping>;

    // Insert a thread. Path name must be unique or empty.
    void insert_thread(const thread_t& t);
    // Insert a whole group of threads. Names should be unique or empty (though
    // they aren't used yet). The indexed graph must be a DAG, at least in the
    // subset traversed by the threads. (Reversing edges are fine, but the
    // threads in a node must all run in the same direction.) This uses a
    // special efficient batch insert algorithm for DAGs that lets us just scan
    // the graph and generate nodes' B_s arrays independently. This must be
    // called only once, and no threads can have been inserted previously.
    // Otherwise the gPBWT data structures will be left in an inconsistent
    // state.
    void insert_threads_into_dag(const vector<thread_t>& t);
    // Read all the threads embedded in the graph.
    list<thread_t> extract_threads() const;
    // Extract a particular thread by name. Name may not be empty.
    // TODO: Actually implement name storage for threads, so we can easily find a thread in the graph by name.
    thread_t extract_thread(const string& name) const;
    // Count matches to a subthread among embedded threads
    size_t count_matches(const thread_t& t) const;
    size_t count_matches(const Path& t) const;

    /**
     * Represents the search state for the graph PBWT, so that you can continue
     * a search with more of a thread, or backtrack.
     *
     * By default, represents an un-started search (with no first visited side)
     * that can be extended to the whole collection of visits to a side.
     */
    struct ThreadSearchState {
        // What side have we just arrived at in the search?
        int64_t current_side = 0;
        // What is the first visit at that side that is selected?
        int64_t range_start = 0;
        // And what is the past-the-last visit that is selected?
        int64_t range_end = numeric_limits<int64_t>::max();

        // How many visits are selected?
        inline int64_t count() {
            return range_end - range_start;
        }

        // Return true if the range has nothing selected.
        inline bool is_empty() {
            return range_end <= range_start;
        }
    };

    // Extend a search with the given section of a thread.
    void extend_search(ThreadSearchState& state, const thread_t& t) const;


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
    // compressed version
    rrr_vector<> s_cbv;
    rrr_vector<>::rank_1_type s_cbv_rank;
    rrr_vector<>::select_1_type s_cbv_select;

    // maintain old ids from input, ranked as in s_iv and s_bv
    int_vector<> i_iv;
    int64_t min_id; // id ranges don't have to start at 0
    int64_t max_id;
    int_vector<> r_iv; // ids-id_min is the rank

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
    //csa_sada<> e_csa;

    // allows lookups of id->rank mapping
    //wt_int<> i_wt;

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

    // Succinct thread storage

    // Threads are haplotype paths in the graph with no edits allowed, starting
    // and stopping at node boundaries.

    // TODO: Explain the whole graph PBWT extension here

    // Basically we keep usage counts for every element in the graph, and and
    // array of next-node-start sides for each side in the graph. We number
    // sides as 2 * xg internal node ID, +1 if it's a right side. This leaves us
    // 0 and 1 free for representing the null destination side and to use as a
    // per-side array run separator, respectively.

    // This holds, for each node and edge, in each direction (with indexes as in
    // the entity vector f_iv, *2, and +1 for reverse), the usage count (i.e.
    // the number of times it is visited by encoded threads). This doesn't have
    // to be dynamic since the length will never change. Remember that entity
    // ranks are 1-based, so if you have an entity rank you have to subtract 1
    // to get its position here. We have to track separately for both directions
    // because, even though when everything is inserted the usage counts in both
    // directions are the same, while we're inserting a thread in one direction
    // and not (yet) the other, the usage counts in both directions will be
    // different.
    int_vector<> h_iv;

    // This (as an extension to the algorithm described in the paper) holds the
    // number of threads beginning at each node. This isn't any extra
    // information relative to what's in the usage count array, but it's cheaper
    // (probably) to maintain this rather than to scan through all the edges on
    // a side every time.
    // ts stands for "thread start"
    int_vector<> ts_iv;

#if GPBWT_MODE == MODE_SDSL
    // We use this for creating the sub-parts of the uncompressed B_s arrays.
    // We don't really support rank and select on this.
    vector<string> bs_arrays;
#endif

    // This holds the concatenated Benedict arrays, with BS_SEPARATOR separating
    // them, and BS_NULL noting the null side (i.e. the thread ends at this
    // node). Instead of holding destination sides, we actually hold the index
    // of the edge that gets taken to the destination side, out of all edges we
    // could take leaving the node. We offset all the values up by 2, to make
    // room for the null sentinel and the separator. Currently the separator
    // isn't used; we just place these by side.
    rank_select_int_vector bs_single_array;

    // A "destination" is either a local edge number + 2, BS_NULL for stopping,
    // or possibly BS_SEPARATOR for cramming multiple Benedict arrays into one.
    using destination_t = size_t;

    // Constants used as sentinels in bs_iv above.
    const static destination_t BS_SEPARATOR;
    const static destination_t BS_NULL;

    // We access this only through these wrapper methods, because we're going to
    // swap out functionality.
    // Sides are from 1-based node ranks, so start at 2.
    // Get the item in a B_s array for a side at an offset.
    destination_t bs_get(int64_t side, int64_t offset) const;
    // Get the rank of a position among positions pointing to a certain
    // destination from a side.
    size_t bs_rank(int64_t side, int64_t offset, destination_t value) const;
    // Set the whole B_s array for a size. May throw an error if B_s for that
    // side has already been set (as overwrite is not necessarily possible).
    void bs_set(int64_t side, vector<destination_t> new_array);
    // Insert into the B_s array for a side
    void bs_insert(int64_t side, int64_t offset, destination_t value);

    // Prepare the B_s array data structures for query. After you call this, you
    // shouldn't call bs_set or bs_insert.
    void bs_bake();
};

class XGPath {
public:
    XGPath(void) { }
    ~XGPath(void) { }
    // Path name is required here only for complaining intelligently when
    // something goes wrong. We can also spit out the total unique members,
    // because in here is the most efficient place to count them.
    XGPath(const string& path_name,
           const vector<trav_t>& path,
           size_t entity_count,
           XG& graph,
           size_t* unique_member_count_out = nullptr);
    // Path names are stored in the XG object, in a compressed fashion, and are
    // not duplicated here.

    sd_vector<> members;
    rank_support_sd<1> members_rank;
    select_support_sd<1> members_select;
    wt_int<> ids;
    sd_vector<> directions; // forward or backward through nodes
    int_vector<> positions;
    int_vector<> ranks;
    bit_vector offsets;
    rank_support_v<1> offsets_rank;
    bit_vector::select_1_type offsets_select;
    void load(istream& in);
    size_t serialize(std::ostream& out,
                     sdsl::structure_tree_node* v = NULL,
                     std::string name = "");
    Mapping mapping(size_t offset); // 0-based
};


Mapping new_mapping(const string& name, int64_t id, size_t rank, bool is_reverse);
void parse_region(const string& target, string& name, int64_t& start, int64_t& end);
void to_text(ostream& out, Graph& graph);

// Serialize a rank_select_int_vector in an SDSL serialization compatible way. Returns the number of bytes written.
size_t serialize(XG::rank_select_int_vector& to_serialize, ostream& out,
    sdsl::structure_tree_node* parent, const std::string name);

// Deserialize a rank_select_int_vector in an SDSL serialization compatible way.
void deserialize(XG::rank_select_int_vector& target, istream& in);

// Determine if two edges are equivalent (the same or one is the reverse of the other)
bool edges_equivalent(const Edge& e1, const Edge& e2);

// Given two equivalent edges, return false if they run in the same direction,
// and true if they are articulated in opposite directions.
bool relative_orientation(const Edge& e1, const Edge& e2);

// Return true if we can only arrive at the start of the given oriented node by
// traversing the given edge in reverse, and false if we can do it by traversing
// the edge forwards. (For single-side self loops, this always beans false.) The
// edge must actually attach to the start of the given oriented node.
bool arrive_by_reverse(const Edge& e, int64_t node_id, bool node_is_reverse);

// Like above, but returns true if we can only ever depart via the edge from the
// node by going in reverse over the edge. Also always false for reversing self
// loops.
bool depart_by_reverse(const Edge& e, int64_t node_id, bool node_is_reverse);

// Make an edge from its fields (generally for comparison)
Edge make_edge(int64_t from, bool from_start, int64_t to, bool to_end);

// Helpers for when we're picking up parts of the graph without returning full Node objects
char reverse_complement(const char& c);
string reverse_complement(const string& seq);

// Position parsing helpers for CLI
void extract_pos(const string& pos_str, int64_t& id, bool& is_rev, size_t& off);
void extract_pos_substr(const string& pos_str, int64_t& id, bool& is_rev, size_t& off, size_t& len);

}

#endif
